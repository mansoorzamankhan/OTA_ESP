#pragma once
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Start the local HTTP OTA server once per boot, after networking is ready.
 * Owns its HTTP server. Requires otadata and two OTA application slots.
 * GET / serves a self-contained upload page, GET /api/info reports firmware,
 * POST /api/update accepts a raw application .bin (not multipart/form-data).
 * A validated upload selects the next boot image and reboots after 2 seconds.
 * Access is controlled by the application's network; HTTP adds no login.
 */
esp_err_t ota_web_start(uint16_t port);
#ifdef __cplusplus
}
#endif
