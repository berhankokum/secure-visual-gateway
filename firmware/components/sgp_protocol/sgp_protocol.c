#include "sgp_protocol.h"


#define SGP_OFFSET_VERSION      0U
#define SGP_OFFSET_TYPE         1U
#define SGP_OFFSET_FLAGS        2U
#define SGP_OFFSET_SESSION      3U
#define SGP_OFFSET_SEQUENCE     7U


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
        ((uint32_t)buffer[2] << 8U)  |
        ((uint32_t)buffer[3]);
}


esp_err_t sgp_encode_header(
    uint8_t *buffer,
    size_t buffer_len,
    const sgp_header_t *header)
{
    if ((buffer == NULL) ||
        (header == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (buffer_len < SGP_HEADER_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    buffer[SGP_OFFSET_VERSION] =
        header->version;

    buffer[SGP_OFFSET_TYPE] =
        header->type;

    buffer[SGP_OFFSET_FLAGS] =
        header->flags;


    encode_u32_be(
        &buffer[SGP_OFFSET_SESSION],
        header->session_id
    );


    encode_u32_be(
        &buffer[SGP_OFFSET_SEQUENCE],
        header->sequence
    );


    return ESP_OK;
}


esp_err_t sgp_decode_header(
    const uint8_t *buffer,
    size_t buffer_len,
    sgp_header_t *header)
{
    if ((buffer == NULL) ||
        (header == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (buffer_len < SGP_HEADER_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    header->version =
        buffer[SGP_OFFSET_VERSION];

    header->type =
        buffer[SGP_OFFSET_TYPE];

    header->flags =
        buffer[SGP_OFFSET_FLAGS];


    header->session_id =
        decode_u32_be(
            &buffer[SGP_OFFSET_SESSION]
        );


    header->sequence =
        decode_u32_be(
            &buffer[SGP_OFFSET_SEQUENCE]
        );


    return ESP_OK;
}