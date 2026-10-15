# OtaWeb

Reusable local HTTP OTA server for ESP-IDF 5.5.1. Copy this directory into your
project's `components/` directory and add `REQUIRES OtaWeb` to the caller.

Initialize your application's networking (Wi-Fi AP or router connection), then
call `ota_web_start(80)` once per boot. The component owns its HTTP server, serves
an embedded offline upload page at `/`, reports firmware at `/api/info`, and
accepts a raw application `.bin` at `POST /api/update` with content type
`application/octet-stream` and Content-Length. Multipart/chunked uploads are not
supported. It adds no separate HTTP login; network access controls OTA access.

Use a partition table with `otadata` and two OTA application slots. Firmware is
streamed to the inactive slot and validated before boot selection. A successful
upload returns HTTP 202 and schedules restart after two seconds. Partial uploads
are aborted and can be retried from zero. Other HTTP requests wait during upload.

NVS and Wi-Fi initialization remain with the application. This repository's
`main/main.c` demonstrates a WPA2 access point. No BLE or external components are
required. See the root README for build, browser, and curl instructions.
