#include "ota_mode.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static esp_err_t start_ota(ota_mode_t mode)
{
    switch (mode) {
#if CONFIG_OTA_MODE_BLE
    case BLE_OTA:
        return ota_ble_application_start();
#elif CONFIG_OTA_MODE_WEB
    case WebServer_OTA:
        return ota_web_application_start();
#endif
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

void app_main(void)
{
#if CONFIG_OTA_MODE_BLE
    const ota_mode_t mode = BLE_OTA;
    const char *mode_name = "BLE_OTA";
#elif CONFIG_OTA_MODE_WEB
    const ota_mode_t mode = WebServer_OTA;
    const char *mode_name = "WebServer_OTA";
#else
#error "Select an OTA implementation in menuconfig"
#endif
    ESP_LOGI("app", "Selected OTA implementation: %s", mode_name);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) err = start_ota(mode);
    if (err != ESP_OK) ESP_LOGE("app", "%s startup failed: %s", mode_name, esp_err_to_name(err));
}
