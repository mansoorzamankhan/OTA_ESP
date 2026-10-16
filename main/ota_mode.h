#pragma once
#include "esp_err.h"

/* Keep these values explicit: they describe the application's OTA transport.
 * menuconfig chooses which implementation is built; changing this enum value
 * alone cannot enable an implementation that was not compiled. */
typedef enum {
    BLE_OTA = 0,
    WebServer_OTA = 1,
} ota_mode_t;

esp_err_t ota_ble_application_start(void);
esp_err_t ota_web_application_start(void);
