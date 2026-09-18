idf_component_register(
    SRCS
        "camera_node.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_timer
        esp_hw_support
        sgp_protocol
        sgp_security
        sgp_crypto
        sgp_image
        sgp_telemetry
        sgp_config
)