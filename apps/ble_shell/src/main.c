/*
 * Runs the Zephyr shell, including the Wi-Fi commands, over NXP's Wireless UART
 * profile, so a BLE client is the board's console. Besides the shell and logs, it
 * reports the Wi-Fi status regularly and whenever the status changes.
 *
 * Each side of the profile is both a GATT server and client. The client writes to the
 * stream characteristic on this board, and this board discovers the same service on
 * the client and writes to its stream characteristic.
 *
 * The link has no pairing or encryption, so any nearby BLE device can use the shell.
 */

#include <stdbool.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/version.h>

LOG_MODULE_REGISTER(ble_shell, LOG_LEVEL_INF);

#define RING_SIZE 2048
#define ATT_WRITE_HEADER 3
#define MAX_CHUNK (CONFIG_BT_L2CAP_TX_MTU - ATT_WRITE_HEADER)
#define DISCOVERY_RETRY_DELAY K_SECONDS(1)
#define DISCOVERY_MAX_ATTEMPTS 10
#define WRITE_RETRY_DELAY_MS 5
#define OUTPUT_GATHER_DELAY_MS 10
#define OUTPUT_FULL_DELAY_MS 5
#define LINE_END_DELAY K_MSEC(200)
#define SENDER_STACK_SIZE 2048
#define SENDER_PRIORITY 5
#define LOG_MESSAGE_QUEUE_SIZE 512
#define STATUS_PERIOD K_SECONDS(30)
#define WIFI_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

#define BT_UUID_WU_SERVICE_VAL BT_UUID_128_ENCODE(0x01ff0100, 0xba5e, 0xf4ee, 0x5ca1, 0xeb1e5e4b1ce0)
#define BT_UUID_WU_STREAM_VAL  BT_UUID_128_ENCODE(0x01ff0101, 0xba5e, 0xf4ee, 0x5ca1, 0xeb1e5e4b1ce0)

static struct bt_uuid_128 wu_service_uuid = BT_UUID_INIT_128(BT_UUID_WU_SERVICE_VAL);
static struct bt_uuid_128 wu_stream_uuid = BT_UUID_INIT_128(BT_UUID_WU_STREAM_VAL);

RING_BUF_DECLARE(input_ring, RING_SIZE);
RING_BUF_DECLARE(output_ring, RING_SIZE);
static K_SEM_DEFINE(output_sem, 0, 1);
static struct k_spinlock input_lock;
static bool input_ends_line = true;

static shell_transport_handler_t shell_handler;
static void *shell_context;

static K_MUTEX_DEFINE(peer_lock);
static struct bt_conn *peer_conn;
static uint16_t peer_stream_handle;

static struct bt_gatt_discover_params discover_params;
static int discovery_attempts;

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

/* Wakes main() to report the status early, and greet a newly connected client. */
static K_SEM_DEFINE(report_sem, 0, 1);
static atomic_t greet_pending;

static bool peer_ready(void)
{
	bool ready;

	k_mutex_lock(&peer_lock, K_FOREVER);
	ready = peer_stream_handle != 0;
	k_mutex_unlock(&peer_lock);

	return ready;
}

static void shell_input(const uint8_t *data, size_t len)
{
	k_spinlock_key_t key = k_spin_lock(&input_lock);

	ring_buf_put(&input_ring, data, len);
	input_ends_line = data[len - 1] == '\r' || data[len - 1] == '\n';
	k_spin_unlock(&input_lock, key);

	if (shell_handler != NULL) {
		shell_handler(SHELL_TRANSPORT_EVT_RX_RDY, shell_context);
	}
}

/* Clients may send a line without a terminator, which the shell would wait for. */
static void line_end(struct k_work *work)
{
	if (!input_ends_line) {
		shell_input((const uint8_t *)"\r", 1);
	}
}

static K_WORK_DELAYABLE_DEFINE(line_end_work, line_end);

static int transport_init(const struct shell_transport *transport, const void *config,
			  shell_transport_handler_t evt_handler, void *context)
{
	shell_handler = evt_handler;
	shell_context = context;

	return 0;
}

static int transport_uninit(const struct shell_transport *transport)
{
	return 0;
}

static int transport_enable(const struct shell_transport *transport, bool blocking)
{
	return 0;
}

static int transport_write(const struct shell_transport *transport, const void *data, size_t length,
			   size_t *cnt)
{
	const uint8_t *bytes = data;
	size_t sent = 0;

	/* Without a ready peer there's no one to read the output, so it is discarded. */
	while (sent < length && peer_ready()) {
		uint32_t put = ring_buf_put(&output_ring, &bytes[sent], length - sent);

		sent += put;
		k_sem_give(&output_sem);
		if (put == 0) {
			k_msleep(OUTPUT_FULL_DELAY_MS);
		}
	}

	*cnt = length;

	return 0;
}

static int transport_read(const struct shell_transport *transport, void *data, size_t length,
			  size_t *cnt)
{
	*cnt = ring_buf_get(&input_ring, data, length);

	return 0;
}

static const struct shell_transport_api transport_api = {
	.init = transport_init,
	.uninit = transport_uninit,
	.enable = transport_enable,
	.write = transport_write,
	.read = transport_read,
};

static struct shell_transport ble_transport = {
	.api = &transport_api,
};

SHELL_DEFINE(ble_shell, "rw612:~$ ", &ble_transport, LOG_MESSAGE_QUEUE_SIZE, 0, SHELL_FLAG_OLF_CRLF);

static ssize_t write_stream(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			    uint16_t len, uint16_t offset, uint8_t flags)
{
	if (len > 0) {
		shell_input(buf, len);
		k_work_reschedule(&line_end_work, LINE_END_DELAY);
	}

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
		/* Discovery ended without a match. The client may still be adding its service. */
		if (discovery_attempts < DISCOVERY_MAX_ATTEMPTS) {
			k_work_schedule(&discovery_work, DISCOVERY_RETRY_DELAY);
		} else {
			printk("Peer has no Wireless UART service\n");
		}
		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_PRIMARY) {
		const struct bt_gatt_service_val *service = attr->user_data;

		params->uuid = &wu_stream_uuid.uuid;
		params->start_handle = attr->handle + 1;
		params->end_handle = service->end_handle;
		params->type = BT_GATT_DISCOVER_CHARACTERISTIC;

		if (bt_gatt_discover(conn, params) != 0) {
			printk("Characteristic discovery failed\n");
		}
		return BT_GATT_ITER_STOP;
	}

	k_mutex_lock(&peer_lock, K_FOREVER);
	peer_stream_handle = bt_gatt_attr_value_handle(attr);
	k_mutex_unlock(&peer_lock);
	printk("Shell ready over BLE\n");

	/* An empty command makes the shell print its prompt on the client. */
	shell_input((const uint8_t *)"\r", 1);

	atomic_set(&greet_pending, 1);
	k_sem_give(&report_sem);

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

	printk(err == 0 ? "Advertising...\n" : "Advertising failed\n");
}

static K_WORK_DEFINE(adv_work, adv_start);

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		printk("Connection failed (0x%02x)\n", err);
		return;
	}

	k_mutex_lock(&peer_lock, K_FOREVER);
	peer_conn = bt_conn_ref(conn);
	peer_stream_handle = 0;
	k_mutex_unlock(&peer_lock);

	printk("Connected\n");
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

	printk("Disconnected (reason 0x%02x)\n", reason);
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	printk("Link parameters: interval %u.%02u ms, latency %u, timeout %u ms\n",
	       (interval * 125) / 100, (interval * 125) % 100, latency, timeout * 10);
}

/* Advertising can only restart once the connection object has been released. */
static void recycled(void)
{
	k_work_submit(&adv_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_updated = le_param_updated,
	.recycled = recycled,
};

/* Sends one chunk of shell output to the peer. */
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

static void sender_thread(void *p1, void *p2, void *p3)
{
	static uint8_t chunk[MAX_CHUNK];
	uint32_t len;

	while (true) {
		k_sem_take(&output_sem, K_FOREVER);

		do {
			/* Let more bytes arrive so that output doesn't become many tiny writes. */
			k_msleep(OUTPUT_GATHER_DELAY_MS);

			len = ring_buf_get(&output_ring, chunk, max_write_len());
			if (len > 0) {
				send_to_peer(chunk, len);
			}
		} while (len > 0);
	}
}

K_THREAD_DEFINE(sender, SENDER_STACK_SIZE, sender_thread, NULL, NULL, NULL, SENDER_PRIORITY, 0, 0);

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	const struct wifi_status *result = cb->info;

	if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		if (result->status != 0) {
			LOG_WRN("Wi-Fi: connection failed (status %d)", result->status);
		} else {
			LOG_INF("Wi-Fi: joined network, waiting for an IP address");
		}
	} else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_INF("Wi-Fi: disconnected (status %d)", result->status);
	}
	k_sem_give(&report_sem);
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	k_sem_give(&report_sem);
}

static void report_wifi_status(void)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_iface_status status = {0};
	struct net_in_addr *addr;
	char ip[NET_IPV4_ADDR_LEN] = "none";

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status)) != 0) {
		LOG_WRN("Wi-Fi: status not available");
		return;
	}

	if (status.state < WIFI_STATE_ASSOCIATED) {
		LOG_INF("Wi-Fi: not connected (%s)", wifi_state_txt(status.state));
		return;
	}

	addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	if (addr != NULL) {
		net_addr_ntop(NET_AF_INET, addr, ip, sizeof(ip));
	}

	LOG_INF("Wi-Fi: connected to %s, %s channel %u, RSSI %d dBm, IP %s", status.ssid,
		wifi_band_txt(status.band), status.channel, status.rssi, ip);
}

int main(void)
{
	static const struct shell_backend_config_flags flags = SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;

	if (shell_init(&ble_shell, NULL, flags, true, LOG_LEVEL_INF) != 0) {
		printk("Shell failed to start\n");
		return 0;
	}

	if (bt_enable(NULL) != 0) {
		printk("Bluetooth failed to start\n");
		return 0;
	}

	k_work_submit(&adv_work);

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	while (true) {
		k_sem_take(&report_sem, STATUS_PERIOD);

		if (atomic_cas(&greet_pending, 1, 0)) {
			LOG_INF("RW612 console on BLE, Zephyr %s, up %u s. Type help for commands.",
				KERNEL_VERSION_STRING, (uint32_t)(k_uptime_get() / MSEC_PER_SEC));
		}
		report_wifi_status();
	}

	return 0;
}
