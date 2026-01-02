# OmniaPi Gateway - Main Component

idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"
        "espnow_master.c"
        "node_manager.c"
        "mqtt_handler.c"
        "webserver.c"
        "storage.c"
        "ota_handler.c"
    INCLUDE_DIRS
        "."
    EMBED_FILES
        "../data/index.html"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        mqtt
        spiffs
        app_update
        json
)
