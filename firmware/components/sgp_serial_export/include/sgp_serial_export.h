#ifndef SGP_SERIAL_EXPORT_H
#define SGP_SERIAL_EXPORT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"


esp_err_t sgp_serial_export_init(void);


esp_err_t sgp_serial_export_submit(
    uint32_t session_id,
    uint32_t image_id,
    uint16_t width,
    uint16_t height,
    uint8_t *data,
    size_t data_len
);


#endif