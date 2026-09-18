idf_component_register(
    SRCS
        "sgp_crypto.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common
        mbedtls
        sgp_protocol
        sgp_config
)