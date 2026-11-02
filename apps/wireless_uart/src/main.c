/*
 * Bridges a UART to BLE using NXP's Wireless UART profile, as used by the Wireless
 * UART screen of the IoT Toolbox app.
 *
 * Each side of the profile is both a GATT server and client. The phone writes to the
 * stream characteristic on this board, and this board discovers the same service on
 * the phone and writes to its stream characteristic. Both use write without response.
 */

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#define RING_SIZE 4096
#define ATT_WRITE_HEADER 3
#define MAX_CHUNK (CONFIG_BT_L2CAP_TX_MTU - ATT_WRITE_HEADER)
#define DISCOVERY_RETRY_DELAY K_SECONDS(1)
#define DISCOVERY_MAX_ATTEMPTS 10
#define WRITE_RETRY_DELAY_MS 5
#define UART_GATHER_DELAY_MS 10

#define BT_UUID_WU_SERVICE_VAL BT_UUID_128_ENCODE(0x01ff0100, 0xba5e, 0xf4ee, 0x5ca1, 0xeb1e5e4b1ce0)
#define BT_UUID_WU_STREAM_VAL  BT_UUID_128_ENCODE(0x01ff0101, 0xba5e, 0xf4ee, 0x5ca1, 0xeb1e5e4b1ce0)

static struct bt_uuid_128 wu_service_uuid = BT_UUID_INIT_128(BT_UUID_WU_SERVICE_VAL);
static struct bt_uuid_128 wu_stream_uuid = BT_UUID_INIT_128(BT_UUID_WU_STREAM_VAL);

static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(app_bridge_uart));

/* uart_rx_ring is filled by the UART interrupt and drained by bridge_thread. */
RING_BUF_DECLARE(uart_rx_ring, RING_SIZE);
RING_BUF_DECLARE(uart_tx_ring, RING_SIZE);
static K_SEM_DEFINE(uart_rx_sem, 0, 1);
static struct k_spinlock uart_tx_lock;

static K_MUTEX_DEFINE(peer_lock);
static struct bt_conn *peer_conn;
static uint16_t peer_stream_handle;

static struct bt_gatt_discover_params discover_params;
static int discovery_attempts;

static void uart_write(const uint8_t *data, size_t len)
{
	k_spinlock_key_t key = k_spin_lock(&uart_tx_lock);

	/* There is no flow control, so bytes beyond the ring's capacity are lost. */
	ring_buf_put(&uart_tx_ring, data, len);
	uart_irq_tx_enable(uart);
	k_spin_unlock(&uart_tx_lock, key);
}

static void print_status(const char *text)
{
	uart_write((const uint8_t *)text, strlen(text));
	uart_write((const uint8_t *)"\r\n", 2);
}

static void uart_isr(const struct device *dev, void *user_data)
{
	uint8_t rx[32];
	uint8_t *tx;
	uint32_t size;
	int count;

	uart_irq_update(dev);

	if (uart_irq_rx_ready(dev) > 0) {
		/* When the ring is full the bytes are read anyway, then lost. */
		while ((count = uart_fifo_read(dev, rx, sizeof(rx))) > 0) {
			ring_buf_put(&uart_rx_ring, rx, count);
		}
		k_sem_give(&uart_rx_sem);
	}

	if (uart_irq_tx_ready(dev) > 0) {
		size = ring_buf_get_ptr(&uart_tx_ring, &tx, 0);
		if (size == 0) {
			uart_irq_tx_disable(dev);
		} else {
			count = uart_fifo_fill(dev, tx, size);
			ring_buf_consume(&uart_tx_ring, count > 0 ? count : 0);
		}
	}
}

static ssize_t write_stream(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			    uint16_t len, uint16_t offset, uint8_t flags)
{
	uart_write(buf, len);

	return len;
}

BT_GATT_SERVICE_DEFINE(wu_service,
	BT_GATT_PRIMARY_SERVICE(&wu_service_uuid),
	BT_GATT_CHARACTERISTIC(&wu_stream_uuid.uuid, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, write_stream, NULL),
);

static void discovery_start(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(discovery_work, discovery_start);

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	if (attr == NULL) {
		/* Discovery ended without a match. The phone may still be adding its service. */
		if (discovery_attempts < DISCOVERY_MAX_ATTEMPTS) {
			k_work_schedule(&discovery_work, DISCOVERY_RETRY_DELAY);
		} else {
			print_status("Peer has no Wireless UART service");
		}
		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_PRIMARY) {
		const struct bt_gatt_service_val *service = attr->user_data;
		int err;

		params->uuid = &wu_stream_uuid.uuid;
		params->start_handle = attr->handle + 1;
		params->end_handle = service->end_handle;
		params->type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, params);
		if (err != 0) {
			print_status("Characteristic discovery failed");
		}
		return BT_GATT_ITER_STOP;
	}

	k_mutex_lock(&peer_lock, K_FOREVER);
	peer_stream_handle = bt_gatt_attr_value_handle(attr);
	k_mutex_unlock(&peer_lock);
	print_status("Wireless UART ready");

	return BT_GATT_ITER_STOP;
}

static void discovery_start(struct k_work *work)
{
	struct bt_conn *conn;

	k_mutex_lock(&peer_lock, K_FOREVER);
	conn = peer_conn != NULL ? bt_conn_ref(peer_conn) : NULL;
	k_mutex_unlock(&peer_lock);

	if (conn == NULL) {
		return;
	}

	discovery_attempts++;
	memset(&discover_params, 0, sizeof(discover_params));
	discover_params.uuid = &wu_service_uuid.uuid;
	discover_params.func = discover_cb;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_PRIMARY;

	if (bt_gatt_discover(conn, &discover_params) != 0 &&
	    discovery_attempts < DISCOVERY_MAX_ATTEMPTS) {
		k_work_schedule(&discovery_work, DISCOVERY_RETRY_DELAY);
	}
	bt_conn_unref(conn);
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_WU_SERVICE_VAL),
	BT_DATA(BT_DATA_NAME_SHORTENED, "NXP_WU", 6),
};

static void adv_start(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);

	print_status(err == 0 ? "Advertising..." : "Advertising failed");
}

static K_WORK_DEFINE(adv_work, adv_start);

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		print_status("Connection failed");
		return;
	}

	k_mutex_lock(&peer_lock, K_FOREVER);
	peer_conn = bt_conn_ref(conn);
	peer_stream_handle = 0;
	k_mutex_unlock(&peer_lock);

	print_status("Connected");
	discovery_attempts = 0;
	k_work_schedule(&discovery_work, K_NO_WAIT);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	k_work_cancel_delayable(&discovery_work);

	k_mutex_lock(&peer_lock, K_FOREVER);
	if (peer_conn != NULL) {
		bt_conn_unref(peer_conn);
		peer_conn = NULL;
	}
	peer_stream_handle = 0;
	k_mutex_unlock(&peer_lock);

	print_status("Disconnected");
}

/* Advertising can only restart once the connection object has been released. */
static void recycled(void)
{
	k_work_submit(&adv_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

/* Sends one chunk of UART data to the peer. Without a ready peer, the data is dropped. */
static void send_to_peer(const uint8_t *data, uint32_t len)
{
	struct bt_conn *conn;
	uint16_t handle;
	int err;

	k_mutex_lock(&peer_lock, K_FOREVER);
	conn = peer_conn != NULL ? bt_conn_ref(peer_conn) : NULL;
	handle = peer_stream_handle;
	k_mutex_unlock(&peer_lock);

	if (conn == NULL) {
		return;
	}

	if (handle != 0) {
		do {
			err = bt_gatt_write_without_response(conn, handle, data, len, false);
			if (err == -ENOMEM || err == -ENOBUFS) {
				k_msleep(WRITE_RETRY_DELAY_MS);
			}
		} while (err == -ENOMEM || err == -ENOBUFS);
	}

	bt_conn_unref(conn);
}

static uint32_t max_write_len(void)
{
	uint32_t len = MAX_CHUNK;

	k_mutex_lock(&peer_lock, K_FOREVER);
	if (peer_conn != NULL) {
		len = MIN(len, bt_gatt_get_mtu(peer_conn) - ATT_WRITE_HEADER);
	}
	k_mutex_unlock(&peer_lock);

	return len;
}

int main(void)
{
	static uint8_t chunk[MAX_CHUNK];
	uint32_t len;

	if (!device_is_ready(uart)) {
		return 0;
	}

	uart_irq_callback_set(uart, uart_isr);
	uart_irq_rx_enable(uart);

	print_status("NXP Wireless UART bridge");

	if (bt_enable(NULL) != 0) {
		print_status("Bluetooth failed to start");
		return 0;
	}

	k_work_submit(&adv_work);

	while (true) {
		k_sem_take(&uart_rx_sem, K_FOREVER);

		do {
			/* Let more bytes arrive so that a burst doesn't become many tiny writes. */
			k_msleep(UART_GATHER_DELAY_MS);

			len = ring_buf_get(&uart_rx_ring, chunk, max_write_len());
			if (len > 0) {
				send_to_peer(chunk, len);
			}
		} while (len > 0);
	}

	return 0;
}
