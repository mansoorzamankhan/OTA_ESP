#include "ota_mode.h"
#include "OtaBle.h"
#include "sdkconfig.h"

esp_err_t ota_ble_application_start(void)
{
    return ota_ble_start(CONFIG_OTA_BLE_NAME);
}
