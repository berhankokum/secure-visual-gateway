#include "sgp_telemetry.h"

#include <string.h>


static void encode_u32_be(
    uint8_t *buffer,
    uint32_t value)
{
    buffer[0] =
        (uint8_t)((value >> 24U) & 0xFFU);

    buffer[1] =
        (uint8_t)((value >> 16U) & 0xFFU);

    buffer[2] =
        (uint8_t)((value >> 8U) & 0xFFU);

    buffer[3] =
        (uint8_t)(value & 0xFFU);
}


static uint32_t decode_u32_be(
    const uint8_t *buffer)
{
    return
        ((uint32_t)buffer[0] << 24U) |
        ((uint32_t)buffer[1] << 16U) |
        ((uint32_t)buffer[2] << 8U) |
        ((uint32_t)buffer[3]);
}


esp_err_t sgp_telemetry_encode(
    uint8_t *buffer,
    size_t buffer_len,
    const sgp_telemetry_t *telemetry)
{
    if ((buffer == NULL) ||
        (telemetry == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (buffer_len <
        SGP_TELEMETRY_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    memset(
        buffer,
        0,
        SGP_TELEMETRY_SIZE
    );


    buffer[0] =
        telemetry->reset_reason;


    encode_u32_be(
        &buffer[4],
        telemetry->uptime_ms
    );


    encode_u32_be(
        &buffer[8],
        telemetry->free_heap
    );


    encode_u32_be(
        &buffer[12],
        telemetry->min_free_heap
    );


    encode_u32_be(
        &buffer[16],
        telemetry->images_sent
    );


    encode_u32_be(
        &buffer[20],
        telemetry->capture_failures
    );


    encode_u32_be(
        &buffer[24],
        telemetry->transfer_failures
    );


    return ESP_OK;
}


esp_err_t sgp_telemetry_decode(
    const uint8_t *buffer,
    size_t buffer_len,
    sgp_telemetry_t *telemetry)
{
    if ((buffer == NULL) ||
        (telemetry == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (buffer_len !=
        SGP_TELEMETRY_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    telemetry->reset_reason =
        buffer[0];


    telemetry->uptime_ms =
        decode_u32_be(
            &buffer[4]
        );


    telemetry->free_heap =
        decode_u32_be(
            &buffer[8]
        );


    telemetry->min_free_heap =
        decode_u32_be(
            &buffer[12]
        );


    telemetry->images_sent =
        decode_u32_be(
            &buffer[16]
        );


    telemetry->capture_failures =
        decode_u32_be(
            &buffer[20]
        );


    telemetry->transfer_failures =
        decode_u32_be(
            &buffer[24]
        );


    return ESP_OK;
}