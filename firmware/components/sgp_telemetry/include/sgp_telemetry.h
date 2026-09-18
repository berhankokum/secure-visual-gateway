#ifndef SGP_TELEMETRY_H
#define SGP_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"


#define SGP_MSG_TELEMETRY          0x30U

#define SGP_TELEMETRY_SIZE         28U


typedef struct
{
    uint8_t reset_reason;

    uint32_t uptime_ms;

    uint32_t free_heap;
    uint32_t min_free_heap;

    uint32_t images_sent;

    uint32_t capture_failures;
    uint32_t transfer_failures;

} sgp_telemetry_t;


esp_err_t sgp_telemetry_encode(
    uint8_t *buffer,
    size_t buffer_len,
    const sgp_telemetry_t *telemetry
);


esp_err_t sgp_telemetry_decode(
    const uint8_t *buffer,
    size_t buffer_len,
    sgp_telemetry_t *telemetry
);


#endif