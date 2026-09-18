idf_component_register(
    SRCS
        "sgp_security.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common
        mbedtls
        sgp_config
)