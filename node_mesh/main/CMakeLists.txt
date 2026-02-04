# OmniaPi Node Mesh - Main Component

idf_component_register(
    SRCS
        "main.c"
        "mesh_node.c"
        "device_relay.c"
        "device_led.c"
        "button_handler.c"
        "commissioning.c"
        "ota_receiver.c"
        "nvs_storage.c"
        "status_led.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_http_client
        app_update
        driver
        esp_timer
        led_strip
        mbedtls
)
