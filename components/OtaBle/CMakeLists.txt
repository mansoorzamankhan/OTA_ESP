set(ota_sources "OtaBle.c" "ota_protocol.c")
if(CONFIG_OTA_MODE_WEB)
    set(ota_sources "")
endif()
idf_component_register(SRCS ${ota_sources} INCLUDE_DIRS "include"
    PRIV_REQUIRES bt app_update esp_timer)
