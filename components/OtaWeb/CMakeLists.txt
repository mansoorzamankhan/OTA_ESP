set(ota_sources "OtaWeb.c")
set(ota_assets "index.html")
if(CONFIG_OTA_MODE_BLE)
    set(ota_sources "")
    set(ota_assets "")
endif()
idf_component_register(SRCS ${ota_sources} INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_server app_update esp_timer json
    EMBED_TXTFILES ${ota_assets})
