#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Start the standalone NimBLE OTA server once per boot, after NVS init.
 * Owns the Bluetooth host. Requires NimBLE and two OTA app slots plus otadata.
 * name: 1..29 bytes. Advertising starts asynchronously after host sync.
 * A verified OTA image is selected for boot and the device restarts.
 */
esp_err_t ota_ble_start(const char *name);
#ifdef __cplusplus
}
#endif
