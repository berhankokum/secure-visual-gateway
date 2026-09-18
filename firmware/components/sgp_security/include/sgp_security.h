#ifndef SGP_SECURITY_H
#define SGP_SECURITY_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"


#define SGP_HMAC_TAG_SIZE 16U


esp_err_t sgp_security_init(void);


esp_err_t sgp_security_sign(
    const uint8_t *data,
    size_t data_len,
    uint8_t *tag,
    size_t tag_size
);


esp_err_t sgp_security_verify(
    const uint8_t *data,
    size_t data_len,
    const uint8_t *tag,
    size_t tag_size
);


#endif