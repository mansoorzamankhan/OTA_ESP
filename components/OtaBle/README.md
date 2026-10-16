# OtaBle

Standalone BLE OTA server for a single ESP32-S3, built with ESP-IDF 5.5.1.
The Espressif **ESP BLE OTA** phone app sends firmware directly to this board.
No separate ESP-AT module or UART is needed.

## Select BLE in this application

Run `idf.py menuconfig`, then select:

```text
OTA application
  OTA implementation
    BLE OTA (ESP BLE OTA phone app)
```

Set the advertising name in the same menu (default `ESP32-OTA`). This enables
Bluetooth with NimBLE and selects the application's `BLE_OTA` enum value.
Only `main_ble.c` and OtaBle's sources are compiled for this mode. The web server
and Wi-Fi AP startup are not used. BLE stack/MTU defaults are supplied by the
application Kconfig. Keep the NimBLE host and peripheral role enabled.

Build and install once over USB, including the shared OTA partition table:

```sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Install the [Espressif ESP BLE OTA app](https://docs.espressif.com/projects/esp-at/en/latest/esp32/Compile_and_Develop/How_to_implement_BLE_OTA_update.html#sample-app),
scan for the configured name, connect, and select **`build/web_ota_esp.bin`**.
The project retains that binary name for both modes. Upload the application image
only, not a bootloader, partition table, or merged image. The board validates the
image and restarts after completion. Interrupted transfers restart from zero after
reconnection. End-to-end phone transfer still needs hardware verification.

## Integrate into a custom application

Copy this entire directory into `components/`, add `REQUIRES OtaBle nvs_flash`
to the caller, and enable these settings in menuconfig:

```ini
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192
```

Provide `otadata` and two application slots large enough for the firmware, and
configure your board's actual flash size. See the root `partitions.csv` for an
example. Initialize NVS, then call once per boot:

```c
#include "OtaBle.h"
#include "nvs_flash.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(ota_ble_start("MyDevice"));
    // Continue your application's initialization.
}
```

The component owns the NimBLE host, GATT registration, and GAP callbacks. Start
it before initializing another Bluetooth stack. There is no public stop function.
Names must contain 1..29 bytes. A successful call starts host initialization;
confirm readiness using the asynchronous advertising log.

The service UUID is `0x8018`, with firmware `0x8020`, progress `0x8021`, command
`0x8022`, and reserved customer data `0x8023`. Sector length, sequence and CRC16
checks precede flash writes. Image validation precedes boot selection. Indications
and notifications are supported for ACKs. Progress is readable; progress events,
customer processing, encrypted/delta OTA, and reconnect resume are unsupported.
This component does not add pairing or a firmware authorization layer.

Run `python3 tests/test_component.py` from the project root for the BLE protocol
tests. Both application modes share the same OTA slots, so firmware for the other
mode can be uploaded through the currently running updater. After reboot, only
the newly selected mode starts.
