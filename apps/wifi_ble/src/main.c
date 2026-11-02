/*
 * Runs Wi-Fi and BLE at the same time. Wi-Fi is controlled from the shell, and a
 * BLE GATT characteristic reports the current Wi-Fi status to connected centrals.
 */

#include <stdio.h>
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

LOG_MODULE_REGISTER(wifi_ble, LOG_LEVEL_INF);

#define WIFI_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)
#define IPV4_EVENTS NET_EVENT_IPV4_ADDR_ADD
#define STATUS_MAX_LEN 48

#define BT_UUID_WIFI_SERVICE_VAL BT_UUID_128_ENCODE(0x52573631, 0x3200, 0x4000, 0x8000, 0x525736313200)
#define BT_UUID_WIFI_STATUS_VAL  BT_UUID_128_ENCODE(0x52573631, 0x3200, 0x4000, 0x8000, 0x525736313201)

static struct bt_uuid_128 wifi_service_uuid = BT_UUID_INIT_128(BT_UUID_WIFI_SERVICE_VAL);
static struct bt_uuid_128 wifi_status_uuid = BT_UUID_INIT_128(BT_UUID_WIFI_STATUS_VAL);

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static K_MUTEX_DEFINE(status_lock);
static char status[STATUS_MAX_LEN] = "disconnected";

static ssize_t read_status(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	ssize_t ret;

	k_mutex_lock(&status_lock, K_FOREVER);
	ret = bt_gatt_attr_read(conn, attr, buf, len, offset, status, strlen(status));
	k_mutex_unlock(&status_lock);

	return ret;
}

BT_GATT_SERVICE_DEFINE(wifi_service,
	BT_GATT_PRIMARY_SERVICE(&wifi_service_uuid),
	BT_GATT_CHARACTERISTIC(&wifi_status_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_status, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static void set_status(const char *text)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	strncpy(status, text, sizeof(status) - 1);
	k_mutex_unlock(&status_lock);

	LOG_INF("Wi-Fi status: %s", text);
	/* Fails harmlessly with -ENOTCONN when no central is subscribed. */
	bt_gatt_notify(NULL, &wifi_service.attrs[1], text, strlen(text));
}

static bool report_ipv4(struct net_if *iface)
{
	struct net_in_addr *addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	char ip[NET_IPV4_ADDR_LEN];
	char text[STATUS_MAX_LEN];

	if (addr == NULL || net_addr_ntop(NET_AF_INET, addr, ip, sizeof(ip)) == NULL) {
		return false;
	}

	snprintf(text, sizeof(text), "connected %s", ip);
	set_status(text);

	return true;
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	const struct wifi_status *result = cb->info;

	if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		if (result->status != 0) {
			set_status("connect failed");
		} else if (!report_ipv4(iface)) {
			/* The driver runs DHCP, and its address can arrive before this event. */
			set_status("associated");
		}
	} else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		set_status("disconnected");
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	report_ipv4(iface);
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_WIFI_SERVICE_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv_start(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err != 0) {
		LOG_ERR("Advertising failed to start (%d)", err);
		return;
	}
	LOG_INF("BLE advertising as \"%s\"", CONFIG_BT_DEVICE_NAME);
}

static K_WORK_DEFINE(adv_work, adv_start);

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		LOG_ERR("BLE connection failed (0x%02x)", err);
		return;
	}
	LOG_INF("BLE connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("BLE disconnected (reason 0x%02x)", reason);
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

int main(void)
{
	int err;

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, IPV4_EVENTS);
	net_mgmt_add_event_callback(&ipv4_cb);

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("Bluetooth failed to start (%d)", err);
		return 0;
	}

	k_work_submit(&adv_work);

	LOG_INF("Ready. Use \"wifi connect\" in the shell to join a network.");

	return 0;
}
