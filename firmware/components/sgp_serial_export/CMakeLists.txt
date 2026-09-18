idf_component_register(
    SRCS
        "sgp_serial_export.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common
        freertos
        heap
        mbedtls
)