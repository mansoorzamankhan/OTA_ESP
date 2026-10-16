set(app_sources "main.c")
if(CONFIG_OTA_MODE_BLE)
    if(NOT CONFIG_BT_NIMBLE_ENABLED)
        message(FATAL_ERROR "BLE OTA requires Bluetooth > Host > NimBLE in menuconfig")
    endif()
    list(APPEND app_sources "main_ble.c")
else()
    list(APPEND app_sources "main_web.c")
endif()
# Keep dependency discovery independent of sdkconfig: ESP-IDF computes component
# requirements before configuration. Inactive OTA components register no sources.
idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "."
    REQUIRES nvs_flash
    PRIV_REQUIRES OtaBle OtaWeb esp_wifi esp_netif esp_event)
