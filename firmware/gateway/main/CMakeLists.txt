idf_component_register(
    SRCS
        "gateway.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        heap

        sgp_protocol
        sgp_security
        sgp_crypto
        sgp_image
        sgp_serial_export
        sgp_telemetry
        sgp_config
)