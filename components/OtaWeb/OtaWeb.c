#include "OtaWeb.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "OtaWeb";
static httpd_handle_t server;
static esp_timer_handle_t reboot_timer;
/* Handlers execute serially on the HTTP server task. */
static bool committed;
extern const unsigned char page_start[] asm("_binary_index_html_start");
extern const unsigned char page_end[] asm("_binary_index_html_end");

static void reboot(void *arg) { (void)arg; esp_restart(); }
static esp_err_t reply(httpd_req_t *req, const char *status, const char *message) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t err = httpd_resp_sendstr(req, message);
    /* Close requests with unread bodies after rejecting an upload. */
    return err == ESP_OK && status[0] == '2' ? ESP_OK : ESP_FAIL;
}
static esp_err_t index_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, (const char *)page_start, page_end - page_start - 1);
}
static esp_err_t info(httpd_req_t *req) {
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    cJSON *json = cJSON_CreateObject();
    if (!json) return reply(req, "500 Internal Server Error", "Out of memory");
    cJSON_AddStringToObject(json, "project", app->project_name);
    cJSON_AddStringToObject(json, "version", app->version);
    cJSON_AddNumberToObject(json, "max_size", next ? next->size : 0);
    cJSON_AddBoolToObject(json, "reboot_pending", committed);
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) return reply(req, "500 Internal Server Error", "Out of memory");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, text);
    free(text);
    return err;
}
static esp_err_t upload(httpd_req_t *req) {
    if (committed) return reply(req, "409 Conflict", "An update is already verified; restart the board.");
    char type[64];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strcmp(type, "application/octet-stream"))
        return reply(req, "415 Unsupported Media Type", "Upload a raw .bin with Content-Type: application/octet-stream.");
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) return reply(req, "500 Internal Server Error", "No OTA slot. Install the full project over USB first.");
    if (!req->content_len || req->content_len > next->size)
        return reply(req, "413 Content Too Large", "Firmware is empty or exceeds the OTA slot size.");
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(next, req->content_len, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin: %s", esp_err_to_name(err));
        return reply(req, "500 Internal Server Error", "Could not prepare the inactive OTA slot.");
    }
    ESP_LOGI(TAG, "Receiving %u bytes into %s", (unsigned)req->content_len, next->label);
    uint8_t data[2048];
    size_t received = 0;
    unsigned timeouts = 0;
    int64_t deadline = esp_timer_get_time() + 180000000;
    while (received < req->content_len) {
        if (esp_timer_get_time() >= deadline) {
            esp_ota_abort(handle);
            return reply(req, "408 Request Timeout", "Upload timed out; the current firmware is unchanged.");
        }
        size_t remaining = req->content_len - received;
        int count = httpd_req_recv(req, (char *)data, remaining < sizeof(data) ? remaining : sizeof(data));
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (count <= 0) {
            esp_ota_abort(handle);
            ESP_LOGW(TAG, "Upload interrupted after %u bytes", (unsigned)received);
            return reply(req, "408 Request Timeout", "Upload interrupted. Reconnect and try again.");
        }
        timeouts = 0;
        err = esp_ota_write(handle, data, count);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            ESP_LOGE(TAG, "OTA write: %s", esp_err_to_name(err));
            return reply(req, "400 Bad Request", "Invalid firmware or flash write failure. Use this board's application .bin.");
        }
        received += count;
    }
    /* esp_ota_end consumes the handle on success and failure. */
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Image validation: %s", esp_err_to_name(err));
        return reply(req, "400 Bad Request", "Firmware validation failed. The current boot image is unchanged.");
    }
    err = esp_ota_set_boot_partition(next);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Boot selection: %s", esp_err_to_name(err));
        return reply(req, "500 Internal Server Error", "Could not select the new firmware for boot.");
    }
    committed = true;
    ESP_LOGI(TAG, "Firmware verified; next boot partition: %s", next->label);
    err = esp_timer_start_once(reboot_timer, 2000000);
    return reply(req, "202 Accepted", err == ESP_OK
        ? "Firmware verified. The board is restarting. Reconnect to Wi-Fi and reload this page."
        : "Firmware verified. Press RESET to boot the new firmware.");
}
esp_err_t ota_web_start(uint16_t port) {
    if (!port) return ESP_ERR_INVALID_ARG;
    if (server) return ESP_ERR_INVALID_STATE;
    if (!esp_ota_get_next_update_partition(NULL)) return ESP_ERR_NOT_FOUND;
    esp_timer_create_args_t timer = {.callback = reboot, .name = "web_ota_reboot"};
    esp_err_t err = esp_timer_create(&timer, &reboot_timer);
    if (err != ESP_OK) return err;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.stack_size = 8192;
    config.recv_wait_timeout = 5;
    config.lru_purge_enable = true;
    err = httpd_start(&server, &config);
    if (err == ESP_OK) {
        const httpd_uri_t routes[] = {
            {.uri = "/", .method = HTTP_GET, .handler = index_page},
            {.uri = "/api/info", .method = HTTP_GET, .handler = info},
            {.uri = "/api/update", .method = HTTP_POST, .handler = upload},
        };
        for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
            err = httpd_register_uri_handler(server, &routes[i]);
            if (err != ESP_OK) break;
        }
    }
    if (err != ESP_OK) {
        if (server) httpd_stop(server);
        server = NULL;
        esp_timer_delete(reboot_timer);
    }
    return err;
}
