#include <string.h>
#include "OtaWeb.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "ota_mode.h"
#include "sdkconfig.h"

static esp_err_t start_access_point(void)
{
    size_t ssid_length = strlen(CONFIG_OTA_WIFI_SSID);
    size_t password_length = strlen(CONFIG_OTA_WIFI_PASSWORD);
    if (!ssid_length || ssid_length > 32 || password_length < 8 || password_length > 63)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!ap) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    wifi_config_t wifi = {.ap = {
        .channel = 1, .max_connection = 4, .authmode = WIFI_AUTH_WPA2_PSK,
    }};
    memcpy(wifi.ap.ssid, CONFIG_OTA_WIFI_SSID, ssid_length);
    wifi.ap.ssid_len = ssid_length;
    memcpy(wifi.ap.password, CONFIG_OTA_WIFI_PASSWORD, password_length);
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &wifi);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) return err;
    esp_netif_ip_info_t ip;
    err = esp_netif_get_ip_info(ap, &ip);
    if (err == ESP_OK)
        ESP_LOGI("app", "Join Wi-Fi '%s', then open http://" IPSTR "/", CONFIG_OTA_WIFI_SSID, IP2STR(&ip.ip));
    return err;
}
esp_err_t ota_web_application_start(void)
{
    esp_err_t err = start_access_point();
    if (err == ESP_OK) err = ota_web_start(80);
    return err;
}
