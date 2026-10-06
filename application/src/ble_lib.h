// Credit: Code Skeleton taken from BME554 Class

#ifndef BLE_LIB_H
#define BLE_LIB_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

/* UUID of the Remote Service */
// Project ID: 065 (3rd entry)
// MFG ID = 0x02DF (4th entry)

#define BT_UUID_REMOTE_SERV_VAL \
	BT_UUID_128_ENCODE(0xe9ea0000, 0xe19b, 0x0065, 0x02DF, 0xc7907585fc48)
#define BT_UUID_REMOTE_SOUND_VAL \
	BT_UUID_128_ENCODE(0xe9ea0001, 0xe19b, 0x0065, 0x02DF, 0xc7907585fc48)
#define BT_UUID_REMOTE_ERR_VAL \
	BT_UUID_128_ENCODE(0xe9ea0002, 0xe19b, 0x0065, 0x02DF, 0xc7907585fc48)
#define BT_UUID_REMOTE_MSG_VAL \
	BT_UUID_128_ENCODE(0xe9ea0003, 0xe19b, 0x0065, 0x02DF, 0xc7907585fc48)

#define BT_UUID_REMOTE_SERVICE BT_UUID_DECLARE_128(BT_UUID_REMOTE_SERV_VAL)
#define BT_UUID_REMOTE_SOUND BT_UUID_DECLARE_128(BT_UUID_REMOTE_SOUND_VAL)
#define BT_UUID_REMOTE_ERR BT_UUID_DECLARE_128(BT_UUID_REMOTE_ERR_VAL)
#define BT_UUID_REMOTE_MSG BT_UUID_DECLARE_128(BT_UUID_REMOTE_MSG_VAL)

// Errr flags for BLE characteristic
enum ble_error_flags {
	ERR_ADC_INIT    = BIT(0),
	ERR_ADC_READ    = BIT(1),
	ERR_CALIBRATION = BIT(2),
};

// Notification flags for BLE characteristic
enum bt_data_notifications_enabled {
    BT_DATA_NOTIFICATIONS_ENABLED,
    BT_DATA_NOTIFICATIONS_DISABLED,
};

// Callback structure for the remote service
struct bt_remote_srv_cb {
    void (*notif_changed)(enum bt_data_notifications_enabled status);
    void (*data_rx)(struct bt_conn *conn, const uint8_t *const data, uint16_t len);
};

/* Payload sent by the sound characteristic to the display. */
struct sound_measurement {
	int32_t db10;
	int32_t frequency_hz;
};

int bluetooth_init(struct bt_remote_srv_cb *remote_cb);
int bluetooth_send_sound_level(int32_t db10, int32_t frequency_hz);
int bluetooth_set_errors(uint32_t flags);

#endif