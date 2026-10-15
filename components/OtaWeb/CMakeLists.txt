idf_component_register(SRCS "OtaWeb.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_server app_update esp_timer json
    EMBED_TXTFILES "index.html")
