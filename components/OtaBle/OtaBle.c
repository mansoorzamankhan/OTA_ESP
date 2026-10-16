#include "OtaBle.h"
#include "ota_protocol.h"
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "OtaBle";
static ota_protocol_t session;
static esp_ota_handle_t writer;
static const esp_partition_t *destination;
static uint16_t handles[4], connection = BLE_HS_CONN_HANDLE_NONE;
static uint8_t address_type;
static bool started, subscribed[4], use_indication[4];
static esp_timer_handle_t reboot_timer;
static int gap_event(struct ble_gap_event *event, void *arg);
static void reboot(void *arg) { (void)arg; esp_restart(); }

static bool begin(uint32_t size) {
    destination = esp_ota_get_next_update_partition(NULL);
    if (!destination || size > destination->size) {
        ESP_LOGE(TAG, "No OTA slot large enough for %" PRIu32 " bytes", size);
        return false;
    }
    esp_err_t err = esp_ota_begin(destination, size, &writer);
    if (err != ESP_OK) ESP_LOGE(TAG, "OTA begin: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "Receiving %" PRIu32 " bytes into %s", size, destination->label);
    return err == ESP_OK;
}
static bool write_sector(const uint8_t *data, size_t size) {
    esp_err_t err = esp_ota_write(writer, data, size);
    if (err != ESP_OK) ESP_LOGE(TAG, "OTA write: %s", esp_err_to_name(err));
    return err == ESP_OK;
}
static void abort_update(void) { esp_ota_abort(writer); writer = 0; }
static bool finish(void) {
    esp_err_t err = esp_ota_end(writer);
    writer = 0;
    if (err == ESP_OK) err = esp_ota_set_boot_partition(destination);
    if (err != ESP_OK) ESP_LOGE(TAG, "Image verification/boot selection: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "Image verified; rebooting shortly");
    return err == ESP_OK;
}
static int send_ack(unsigned index, const uint8_t ack[20]) {
    struct os_mbuf *om = ble_hs_mbuf_from_flat(ack, 20);
    if (!om) return BLE_ATT_ERR_INSUFFICIENT_RES;
    int rc = use_indication[index]
        ? ble_gatts_indicate_custom(connection, handles[index], om)
        : ble_gatts_notify_custom(connection, handles[index], om);
    if (rc) ESP_LOGW(TAG, "ACK send failed: %d", rc);
    return rc ? BLE_ATT_ERR_UNLIKELY : 0;
}
static int access_chr(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr;
    unsigned index = (uintptr_t)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (index == 1) {
            uint8_t progress[10];
            ota_protocol_progress(&session, progress);
            return os_mbuf_append(ctxt->om, progress, sizeof(progress)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        }
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t packet[512], ack[20];
    uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
    if (length > sizeof(packet) || os_mbuf_copydata(ctxt->om, 0, length, packet))
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (index == 3) return 0;
    if (!subscribed[0] || !subscribed[2]) return BLE_ATT_ERR_UNLIKELY;
    int rc = 0;
    if (index == 2) {
        if (!ota_protocol_command(&session, packet, length, ack))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        rc = send_ack(2, ack);
    } else if (index == 0) {
        if (ota_protocol_data(&session, packet, length, ack)) rc = send_ack(0, ack);
        if (session.complete && !esp_timer_is_active(reboot_timer)) {
            esp_err_t err = esp_timer_start_once(reboot_timer, 2000000);
            if (err != ESP_OK) ESP_LOGE(TAG, "Restart timer failed; press RESET: %s", esp_err_to_name(err));
        }
    }
    return rc;
}
static int read_version(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    const char *version = esp_app_get_description()->version;
    return os_mbuf_append(ctxt->om, version, strlen(version)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180a),
     .characteristics = (struct ble_gatt_chr_def[]) {
         {.uuid = BLE_UUID16_DECLARE(0x2a26), .access_cb = read_version, .flags = BLE_GATT_CHR_F_READ}, {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x8018),
     .characteristics = (struct ble_gatt_chr_def[]) {
         {.uuid = BLE_UUID16_DECLARE(0x8020), .access_cb = access_chr, .arg = (void *)0,
          .val_handle = &handles[0], .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_NOTIFY},
         {.uuid = BLE_UUID16_DECLARE(0x8021), .access_cb = access_chr, .arg = (void *)1,
          .val_handle = &handles[1], .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_NOTIFY},
         {.uuid = BLE_UUID16_DECLARE(0x8022), .access_cb = access_chr, .arg = (void *)2,
          .val_handle = &handles[2], .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_NOTIFY},
         {.uuid = BLE_UUID16_DECLARE(0x8023), .access_cb = access_chr, .arg = (void *)3,
          .val_handle = &handles[3], .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_NOTIFY}, {0}}}, {0}
};
static void advertise(void) {
    struct ble_hs_adv_fields fields = {0}, scan = {0};
    ble_uuid16_t uuid = BLE_UUID16_INIT(0x8018);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids16 = &uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    scan.name = (const uint8_t *)ble_svc_gap_device_name();
    scan.name_len = strlen((const char *)scan.name);
    scan.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (!rc) rc = ble_gap_adv_rsp_set_fields(&scan);
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    if (!rc) rc = ble_gap_adv_start(address_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc) ESP_LOGE(TAG, "Advertising failed: %d", rc);
    else ESP_LOGI(TAG, "Advertising as %s; connect using ESP BLE OTA app", scan.name);
}
static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) advertise();
        else { connection = event->connect.conn_handle; ESP_LOGI(TAG, "Phone connected"); }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Phone disconnected");
        connection = BLE_HS_CONN_HANDLE_NONE;
        memset(subscribed, 0, sizeof(subscribed));
        if (!session.complete) { ota_protocol_reset(&session); advertise(); }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!session.complete) advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        for (unsigned i = 0; i < 4; i++) if (event->subscribe.attr_handle == handles[i]) {
            subscribed[i] = event->subscribe.cur_indicate || event->subscribe.cur_notify;
            use_indication[i] = event->subscribe.cur_indicate;
        }
        break;
    default: break;
    }
    return 0;
}
static void synchronized(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &address_type);
    if (rc) ESP_LOGE(TAG, "BLE address setup failed: %d", rc);
    else advertise();
}
static void reset_host(int reason) {
    ESP_LOGE(TAG, "BLE host reset: %d", reason);
    if (!session.complete) ota_protocol_reset(&session);
    connection = BLE_HS_CONN_HANDLE_NONE;
    memset(subscribed, 0, sizeof(subscribed));
}
static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}
esp_err_t ota_ble_start(const char *name) {
    if (!name || !strlen(name) || strlen(name) > 29) return ESP_ERR_INVALID_ARG;
    if (started) return ESP_ERR_INVALID_STATE;
    if (!esp_ota_get_next_update_partition(NULL)) return ESP_ERR_NOT_FOUND;
    esp_timer_create_args_t timer = {.callback = reboot, .name = "ota_reboot"};
    esp_err_t err = esp_timer_create(&timer, &reboot_timer);
    if (err != ESP_OK) return err;
    err = nimble_port_init();
    if (err != ESP_OK) { esp_timer_delete(reboot_timer); return err; }
    session.backend = (ota_backend_t){begin, write_sector, finish, abort_update};
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(name);
    if (!rc) rc = ble_att_set_preferred_mtu(512);
    if (!rc) rc = ble_gatts_count_cfg(services);
    if (!rc) rc = ble_gatts_add_svcs(services);
    if (rc) {
        nimble_port_deinit();
        esp_timer_delete(reboot_timer);
        return ESP_FAIL;
    }
    ble_hs_cfg.sync_cb = synchronized;
    ble_hs_cfg.reset_cb = reset_host;
    started = true;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
