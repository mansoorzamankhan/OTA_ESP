# Use OtaWeb in a custom application

`OtaWeb` adds a local browser-based firmware updater to an ESP-IDF application.
It serves an offline upload page, streams firmware into the inactive OTA slot,
validates the image, and restarts the board after a successful update.

Built with **ESP-IDF 5.5.1 on ESP32-S3**. The application owns NVS and networking.
Use either a device Wi-Fi access point or a connection to an existing router.
No BLE or external component downloads are required.

## Select web OTA in this sample

Open `idf.py menuconfig` and select **OTA application > OTA implementation >
Wi-Fi web server OTA (browser)**. Set the Wi-Fi name and password in the same
menu. This selects `WebServer_OTA` in the application enum and compiles
`main/main_web.c` and OtaWeb. BLE startup and OtaBle sources are excluded.
When copying OtaWeb into another project, this sample's menu and enum are optional;
your application can call `ota_web_start()` directly as described below.

## 1. Copy the component

Copy this entire directory into your project's `components/` folder. Include
`index.html`: the component embeds it into the firmware at build time.

```text
my_application/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   └── main.c
└── components/
    └── OtaWeb/
        ├── CMakeLists.txt
        ├── OtaWeb.c
        ├── index.html
        └── include/
            └── OtaWeb.h
```

Add `OtaWeb` to the caller's existing CMake dependency list. For example, if
`main` also initializes Wi-Fi and NVS:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES OtaWeb nvs_flash esp_wifi esp_netif esp_event
)
```

OtaWeb declares its HTTP server, OTA, timer, and JSON dependencies itself.
You do not need the other components or root CMake configuration from this project.
C++ applications can include `OtaWeb.h` directly.

## 2. Configure OTA partitions

Provide `otadata` and two application slots, each large enough for the complete
application. An example project-root `partitions.csv`:

```csv
# Name, Type, SubType, Offset, Size
nvs,data,nvs,0x9000,0x6000
otadata,data,ota,0xf000,0x2000
phy_init,data,phy,0x11000,0x1000
ota_0,app,ota_0,0x20000,0x1E0000
ota_1,app,ota_1,0x200000,0x1E0000
```

This layout needs at least **4 MB of physical flash** and provides two 1920 KiB
application slots. Adjust it to preserve your existing data partitions and avoid
overlaps. Select your board's actual flash capacity under `Serial flasher config`
in menuconfig. This repository uses 32 MB; another board may need a different
setting. Moving an existing partition does not migrate its contents.

For a new project, add to `sdkconfig.defaults`:

```ini
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

For an existing project, select the custom partition table in `idf.py menuconfig`.
Defaults do not override settings already saved in `sdkconfig`.

## 3. Start after networking is ready

Initialize NVS, netif, the event loop, and Wi-Fi through your application's normal
startup flow. Then call `ota_web_start()` once per boot:

```c
#include "OtaWeb.h"
#include "esp_log.h"

/* Call this after your application's network is ready. */
static void start_update_server(void)
{
    esp_err_t err = ota_web_start(80);
    if (err != ESP_OK) {
        ESP_LOGE("app", "OTA startup failed: %s", esp_err_to_name(err));
    }
}
```

The function starts an HTTP server task and returns, allowing the application to
continue running. It does not initialize or erase NVS, create interfaces, or
configure Wi-Fi.

### Wi-Fi access point

Start Wi-Fi in AP mode, then call `start_update_server()`. Connect a phone or
computer to that AP and open its IP address in a browser. ESP-IDF normally uses
`http://192.168.4.1/`; use your configured address if different.

For a complete AP startup example, see [`main/main.c`](../../main/main.c).
That example also uses settings from
[`main/Kconfig.projbuild`](../../main/Kconfig.projbuild); copy or replace those
settings if you copy the application code. OtaWeb has no built-in Wi-Fi credentials.

### Existing router (station mode)

Start OtaWeb after receiving `IP_EVENT_STA_GOT_IP`. Register this handler once
with your existing event loop before starting Wi-Fi. Keep your normal Wi-Fi
connection and reconnection logic.

```c
#include <stdbool.h>
#include "OtaWeb.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

static void on_station_ip(void *arg, esp_event_base_t base,
                          int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    (void)event_id;
    static bool server_started;
    ip_event_got_ip_t *event = event_data;

    if (!server_started) {
        esp_err_t err = ota_web_start(80);
        if (err != ESP_OK) {
            ESP_LOGE("app", "OTA startup failed: %s", esp_err_to_name(err));
            return;
        }
        server_started = true;
    }
    ESP_LOGI("app", "OTA page: http://" IPSTR "/", IP2STR(&event->ip_info.ip));
}

/* After creating the default event loop:
 * ESP_ERROR_CHECK(esp_event_handler_register(
 *     IP_EVENT, IP_EVENT_STA_GOT_IP, on_station_ip, NULL));
 */
```

Open the logged address from a device on the same network. The server remains
running across station reconnections; do not start another instance. Its IP may
change after reconnecting.

### Already running an HTTP server?

OtaWeb creates its own server; it does not accept an existing server handle.
Choose an unused listening port, such as `ota_web_start(8080)`, and open
`http://<device-ip>:8080/`. Each ESP-IDF server also needs a unique control port.
OtaWeb uses the default control port, so configure your existing server's
`ctrl_port` differently before running both. Alternatively, adapt OtaWeb to
register its routes on your existing server.

## 4. Install once over USB

Activate your ESP-IDF environment and run:

```sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace the chip target and port for your board. The first full flash installs
the bootloader, OTA partition table, metadata, and application. Web uploads only
replace an application image; they cannot change the bootloader or partition table.

## 5. Update your application through the browser

1. Build the new firmware with `idf.py build`.
2. Connect to the device's AP, or to the same router as the device.
3. Open the device's IP and port using **HTTP**.
4. Select **`build/<project-name>.bin`** and click **Upload firmware**.
5. Wait for verification and automatic restart, then reconnect and reload the page.

For this repository, the application is `build/web_ota_esp.bin`. In your project,
use the binary named after your CMake `project(...)`. Do not upload a merged flash
image, bootloader, or partition table.

Keep OtaWeb and network startup in future firmware to allow subsequent web updates.
Set the ESP-IDF project version, for example through a root `version.txt`, to
identify the running build on the page. The progress bar shows browser upload
progress; wait for the separate verification-success message.

## API and lifecycle

Public API: [`include/OtaWeb.h`](include/OtaWeb.h).

```c
esp_err_t ota_web_start(uint16_t port);
```

| Result | Meaning |
| --- | --- |
| `ESP_OK` | Server and routes started |
| `ESP_ERR_INVALID_ARG` | Port is zero |
| `ESP_ERR_INVALID_STATE` | OtaWeb is already running |
| `ESP_ERR_NOT_FOUND` | No next OTA application slot exists |
| Other error | Timer, HTTP server, or route initialization failed |

Call once per boot. There is currently no public stop function.

| Method | Route | Purpose |
| --- | --- | --- |
| GET | `/` | Offline firmware upload page |
| GET | `/api/info` | Project, version, maximum image size, restart state |
| POST | `/api/update` | Raw application firmware bytes |

Command-line upload from a device on the same network:

```sh
curl --fail-with-body \
  -H 'Content-Type: application/octet-stream' \
  --data-binary @build/my_application.bin \
  http://192.168.4.1/api/update
```

Replace the filename, address, and port. POST requires Content-Length and the
exact content type `application/octet-stream`. Multipart forms and chunked request
bodies are unsupported.

An interrupted upload or invalid image leaves the current boot image selected.
Retry interrupted uploads from zero. Validation finishes before boot selection.
HTTP `202 Accepted` means the new image was selected and restart is scheduled for
two seconds later. If restart scheduling fails, the response asks you to press
RESET. Further uploads are rejected until reboot.

Uploads have a 180-second deadline and allow five consecutive socket timeouts.
The server handles uploads synchronously; other requests on this server wait.
Other application tasks continue running. Post-boot health checks and rollback
confirmation are not implemented by OtaWeb. If your application enables ESP-IDF
rollback, integrate its own image-confirmation flow.

## Customize the page and access

Edit [`index.html`](index.html), then rebuild. All assets are embedded and work
offline. If you change the routes or request format, update the browser code and
HTTP handlers together.

The server uses HTTP with no separate login. Devices that can reach the endpoint
can submit firmware. Apply your application's network access policy, such as a
password-protected AP. Authentication or HTTPS requires extending the component.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Page unreachable | Network connection, logged IP, port, and whether the phone stayed connected despite no internet |
| `ESP_ERR_NOT_FOUND` | Installed partition table has two OTA app slots |
| HTTP server startup fails | Listening/control ports are available |
| Image too large | Complete application fits the installed OTA slot |
| HTTP 415 | Raw bytes with `application/octet-stream`, not multipart |
| Validation fails | Complete application image for the correct chip |
| Connection lost after success | Reconnect after reboot and check the running version |

In this repository, `python3 tests/test_web_ota.py` exercises the actual upload
handler against fake HTTP/flash backends. Build and handler tests pass; verify
the network/upload/reboot path on your custom application's physical hardware.
