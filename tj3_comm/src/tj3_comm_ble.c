#include <tj3_comm/ble.h>

#include <errno.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(tj3_comm_ble, LOG_LEVEL_INF);

/* Simple custom service: 1 write (RX) + 1 notify (TX)
 * UUIDs are locally defined (no external dependency).
 */
static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(
    0x9e, 0x12, 0x5a, 0x21, 0x2b, 0x37, 0x43, 0x7a,
    0xb3, 0x8b, 0x4f, 0x6c, 0x00, 0x00, 0x10, 0x01);

static struct bt_uuid_128 rx_uuid = BT_UUID_INIT_128(
    0x9e, 0x12, 0x5a, 0x21, 0x2b, 0x37, 0x43, 0x7a,
    0xb3, 0x8b, 0x4f, 0x6c, 0x00, 0x00, 0x10, 0x02);

static struct bt_uuid_128 tx_uuid = BT_UUID_INIT_128(
    0x9e, 0x12, 0x5a, 0x21, 0x2b, 0x37, 0x43, 0x7a,
    0xb3, 0x8b, 0x4f, 0x6c, 0x00, 0x00, 0x10, 0x03);

static struct tj3_comm_parser *g_parser;
static struct tj3_comm_parser rx_parser;
static struct bt_conn *current_conn;
static bool notify_enabled;

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_WRN("BLE connect failed (%u)", err);
        return;
    }

    current_conn = bt_conn_ref(conn);
    LOG_INF("BLE connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    LOG_INF("BLE disconnected (%u)", reason);
    notify_enabled = false;

    if (current_conn)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
}

BT_CONN_CB_DEFINE(conn_cb) = {
    .connected = connected,
    .disconnected = disconnected,
};

static void ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("BLE notify %s", notify_enabled ? "enabled" : "disabled");
}

static ssize_t rx_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    if (g_parser == NULL || buf == NULL || len == 0)
    {
        return len;
    }

    (void)tj3_comm_parser_feed(g_parser, (const uint8_t *)buf, len);
    return len;
}

BT_GATT_SERVICE_DEFINE(tj3_comm_svc,
                       BT_GATT_PRIMARY_SERVICE(&service_uuid),
                       BT_GATT_CHARACTERISTIC(&rx_uuid.uuid, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE, NULL, rx_write_cb, NULL),
                       BT_GATT_CHARACTERISTIC(&tx_uuid.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

int tj3_comm_ble_init(tj3_comm_frame_rx_cb on_frame, void *user_data)
{
    tj3_comm_parser_init(&rx_parser, on_frame, user_data);
    g_parser = &rx_parser;

    int err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("bt_enable failed (%d)", err);
        return err;
    }

    struct bt_le_adv_param adv_param = {
        .options = BT_LE_ADV_OPT_CONN,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .id = BT_ID_DEFAULT,
    };

    const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA(BT_DATA_UUID128_ALL, service_uuid.val, 16),
    };

    err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("bt_le_adv_start failed (%d)", err);
        return err;
    }

    LOG_INF("BLE advertising started");
    return 0;
}

int tj3_comm_ble_notify_frame(uint8_t op, const uint8_t *payload, uint16_t payload_len)
{
    if (!notify_enabled || current_conn == NULL)
    {
        return -ENOTCONN;
    }

    uint8_t frame[CONFIG_TJ3_COMM_MAX_PAYLOAD + 7u];
    uint16_t frame_len = 0;
    int err = tj3_comm_frame_encode(op, payload, payload_len, frame, sizeof(frame), &frame_len);
    if (err)
    {
        return err;
    }

    /* TX characteristic value attribute is at index 4 in attrs[]:
     * 0: service
     * 1: RX decl
     * 2: RX value
     * 3: TX decl
     * 4: TX value
     */
    return bt_gatt_notify(current_conn, &tj3_comm_svc.attrs[4], frame, frame_len);
}
