// Credit: Code Skeleton taken from BME554 Class
#include "ble_lib.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(blelib, LOG_LEVEL_INF);
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define MAX_MSG_LEN 64

static struct sound_measurement sound_measurement;
static uint32_t error_flags;
static bool sound_notify_enabled;
static bool err_notify_enabled;

static struct bt_conn *current_conn;
static struct bt_remote_srv_cb remote_service_callbacks;
static struct k_work adv_work;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_REMOTE_SERV_VAL),
};

static ssize_t read_sound_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &sound_measurement, sizeof(sound_measurement));
}

static ssize_t read_error_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &error_flags, sizeof(error_flags));
}

static ssize_t on_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	char text[MAX_MSG_LEN + 1];
	uint16_t copy_len = MIN(len, MAX_MSG_LEN);

	memcpy(text, buf, copy_len);
	text[copy_len] = '\0';
	LOG_INF("Message from phone: %s", text);

	if (remote_service_callbacks.data_rx) {
		remote_service_callbacks.data_rx(conn, buf, len);
	}

	return len;
}

/*
 * Called when the phone turns Sound level notifications on or off.
 *
 * Args:
 *     attr: The CCC attribute that changed.
 *     value: BT_GATT_CCC_NOTIFY if enabled, 0 if disabled.
 */
static void sound_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	sound_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("Sound level notifications %s",
		sound_notify_enabled ? "enabled" : "disabled");

	if (remote_service_callbacks.notif_changed) {
		remote_service_callbacks.notif_changed(sound_notify_enabled ?
			BT_DATA_NOTIFICATIONS_ENABLED : BT_DATA_NOTIFICATIONS_DISABLED);
	}
}

/*
 * Called when the phone turns Errors notifications on or off.
 *
 * Args:
 *     attr: The CCC attribute that changed.
 *     value: BT_GATT_CCC_NOTIFY if enabled, 0 if disabled.
 */
static void err_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	err_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("Error notifications %s",
		err_notify_enabled ? "enabled" : "disabled");
}

/*
 * The Remote Service GATT table.
 *
 * Attribute index layout (used when sending notifications):
 *     [0] Service declaration
 *     [1] Sound level declaration   [2] Sound level value   [3] Sound CCC
 *     [4] Errors declaration        [5] Errors value        [6] Errors CCC
 *     [7] Message declaration       [8] Message value
 */
BT_GATT_SERVICE_DEFINE(remote_srv,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_REMOTE_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_REMOTE_SOUND,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_sound_cb, NULL, NULL),
	BT_GATT_CCC(sound_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_REMOTE_ERR,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_error_cb, NULL, NULL),
	BT_GATT_CCC(err_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_REMOTE_MSG,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE,
			       NULL, on_write, NULL),
);

/* Attribute indexes of the values that get notified (see table above). */
#define SOUND_ATTR_IDX 2
#define ERR_ATTR_IDX   5

/*
 * Start connectable advertising.
 *
 * Runs on the system work queue. Called at startup and again after each
 * disconnect so the phone can reconnect.
 */
static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
				  sd, ARRAY_SIZE(sd));

	if (err) {
		LOG_ERR("Could not start advertising (%d)", err);
		return;
	}

	LOG_INF("Advertising as \"%s\"", DEVICE_NAME);
}

/*
 * Called when a phone connects.
 *
 * Args:
 *     conn: The new connection.
 *     err: 0 on success, otherwise an HCI error code.
 */
static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection error: %u", err);
		return;
	}

	LOG_INF("BT connected");
	current_conn = bt_conn_ref(conn);
}

/*
 * Called when the phone disconnects.
 *
 * Args:
 *     conn: The connection that ended.
 *     reason: HCI reason code for the disconnect.
 */
static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("BT disconnected (reason: %u)", reason);

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	sound_notify_enabled = false;
	err_notify_enabled = false;
}

/*
 * Called once the old connection is fully cleaned up.
 *
 * This is the safe point to start advertising again.
 */
static void on_recycled(void)
{
	k_work_submit(&adv_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.recycled = on_recycled,
};

int bluetooth_init(struct bt_remote_srv_cb *remote_cb)
{
	LOG_INF("Initializing Bluetooth");

	if (remote_cb) {
		remote_service_callbacks = *remote_cb;
	}

	/* With NULL, bt_enable() waits until Bluetooth is ready. */
	int err = bt_enable(NULL);

	if (err) {
		LOG_ERR("bt_enable returned %d", err);
		return err;
	}

	k_work_init(&adv_work, adv_work_handler);
	k_work_submit(&adv_work);

	return 0;
}

int bluetooth_send_sound_level(int32_t db10, int32_t frequency_hz)
{
	sound_measurement.db10 = db10;
	sound_measurement.frequency_hz = frequency_hz;

	if (!current_conn || !sound_notify_enabled) {
		return 0;
	}

	return bt_gatt_notify(current_conn, &remote_srv.attrs[SOUND_ATTR_IDX],
			      &sound_measurement, sizeof(sound_measurement));
}

int bluetooth_set_errors(uint32_t flags)
{
	error_flags |= flags;

	if (!current_conn || !err_notify_enabled) {
		return 0;
	}

	return bt_gatt_notify(current_conn, &remote_srv.attrs[ERR_ATTR_IDX],
			      &error_flags, sizeof(error_flags));
}