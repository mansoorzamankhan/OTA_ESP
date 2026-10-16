# Selectable BLE and web OTA for ESP32-S3

Choose **BLE OTA** or **Wi-Fi web server OTA** at build time. Both run directly
on the single ESP32-S3 and use the same OTA flash slots.

## Choose the OTA implementation

Run `idf.py menuconfig` and open **OTA application > OTA implementation**:

| Selection | Application enum | Startup source | Component |
| --- | --- | --- | --- |
| BLE OTA (ESP BLE OTA phone app) | `BLE_OTA` | `main/main_ble.c` | `OtaBle` |
| Wi-Fi web server OTA (browser), default | `WebServer_OTA` | `main/main_web.c` | `OtaWeb` |

The menu also shows the selected mode's advertising name or Wi-Fi credentials.
`main/ota_mode.h` defines the enum. `main/main.c` maps menuconfig to that enum and
starts the chosen implementation after NVS initialization. Only the selected
implementation's sources are compiled. The BLE mode uses NimBLE; switching to
web mode does not initialize Bluetooth.

Changing the enum alone does not compile another transport. To switch modes,
change menuconfig, build, and install the new application. You can upload the new
image through the currently running updater; the selected mode starts after reboot.

For BLE setup, phone-app instructions, and custom integration, see
[OtaBle](components/OtaBle/README.md). The following browser instructions apply
to **WebServer_OTA**. The project binary remains `build/web_ota_esp.bin` in both modes.

## First installation

Built with ESP-IDF 5.5.1 for the detected ESP32-S3 board with 32 MB flash.
The custom partition table contains OTA metadata and two 1920 KiB application
slots in the first 4 MB of flash.

```sh
. /home/mansoor/.espressif/tools/activate_idf_v5.5.1.sh
idf.py set-target esp32s3
idf.py menuconfig
# OTA application: select mode and configure its name/credentials
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Flash the complete project over USB once to install the web server and its OTA
partition layout. A previous BLE installation using this same `partitions.csv`
already has the correct slots, but still needs this new application installed.
Close any serial monitor before flashing. No full-chip erase is required.

## Update through a browser

1. Power the board and connect to Wi-Fi **ESP32-OTA**.
2. Enter password **esp32ota123** (change it under `OTA application` in menuconfig).
3. Stay connected if your phone reports that this network has no internet.
4. Open **http://192.168.4.1/** using HTTP. The actual IP is also printed in the
   serial log. This is a direct address; no captive-portal popup is provided.
5. Select **`build/web_ota_esp.bin`**, then choose **Upload firmware**.
6. Keep power and Wi-Fi connected while the page uploads and the board verifies
   the image. After success, the board automatically restarts after two seconds.
7. Reconnect to its Wi-Fi and reload the page to see the running firmware version.

Upload the application image only, built for this chip and partition layout.
Do not upload a merged flash image, bootloader, or partition table. Keep `OtaWeb`
and networking startup in future firmware if you want to continue web updates.
To distinguish builds, set the ESP-IDF project version (for example via a root
`version.txt` file) before building the next application.

The web page and scripts are embedded in firmware and work entirely offline.
The progress bar shows browser upload progress; the separate verification message
indicates that the board is validating the received image.

## Reuse OtaWeb in another ESP-IDF project

For step-by-step integration instructions, see
[Use OtaWeb in a custom application](components/OtaWeb/README.md).

Copy `components/OtaWeb` into your project's `components/` directory and add
`REQUIRES OtaWeb` to the component that calls it. Initialize your networking first,
then call:

```c
#include "OtaWeb.h"

ESP_ERROR_CHECK(ota_web_start(80));
```

It starts its own HTTP server once per boot. It does not initialize NVS, Wi-Fi,
netif, or the event loop; the application owns those. This example uses a WPA2
access point, while another application can call it after connecting to an
existing router and obtaining an IP address. The server then uses that IP.

Provide `otadata` and two OTA application partitions large enough for your image.
See `partitions.csv` and `sdkconfig.defaults`. The server requires the built-in
ESP-IDF HTTP server, application update, timer, and JSON components, without
external downloads. In web mode, OtaBle sources and BLE startup are excluded; Bluetooth is not used.

## HTTP API

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/` | Self-contained firmware upload page |
| GET | `/api/info` | Project, version, maximum image size, and restart state |
| POST | `/api/update` | Raw application image with `Content-Type: application/octet-stream` |

Example from a computer connected to the device:

```sh
curl --fail-with-body -H 'Content-Type: application/octet-stream' \
  --data-binary @build/web_ota_esp.bin http://192.168.4.1/api/update
```

Send a Content-Length; chunked request bodies and multipart uploads are not
supported. A `202` response means the image was validated and selected for the
next boot. If automatic restart cannot be scheduled, the response instructs you
to press RESET. The server handles one upload at a time and rejects additional
updates after boot selection until restart.

An incomplete upload or invalid image leaves the current boot image selected.
Disconnected/timed-out uploads abort their OTA handle and can be retried from
zero. The upload allows up to 180 seconds and five consecutive socket timeouts.
The HTTP server task is occupied during upload, so other requests wait.
Wi-Fi credentials control access to this local update network; the HTTP endpoint
adds no separate login or TLS. Devices on this network can submit firmware.
NVS initialization errors are logged without automatically erasing NVS.

## Validation

```sh
python3 tests/test_web_ota.py
```

This compiles the actual HTTP upload handler against fake HTTP/flash backends with
AddressSanitizer and UndefinedBehaviorSanitizer. It covers raw-body streaming,
wrong content types and sizes, interruptions, timeouts, flash/validation failures,
boot selection, duplicate uploads, and restart scheduling. Leak detection is
disabled for the sandbox. `python3 tests/test_component.py` covers the BLE OTA protocol.

The ESP32-S3 firmware build and upload-handler tests pass. Actual Wi-Fi/browser
upload and reboot still need verification on the board.

## Build isolated configurations (optional)

These presets choose the same modes as menuconfig, with separate configuration
files and build directories so neither overwrites the other:

```sh
idf.py -B build-web -DSDKCONFIG="$PWD/sdkconfig.web" \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.web.defaults" build
idf.py -B build-ble -DSDKCONFIG="$PWD/sdkconfig.ble" \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ble.defaults" build
```

The images are `build-web/web_ota_esp.bin` and `build-ble/web_ota_esp.bin`.
Defaults initialize fresh configurations. For an existing configuration, use
menuconfig with the corresponding build directory and SDKCONFIG arguments to
change its selection.
