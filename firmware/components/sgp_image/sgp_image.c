#include "sgp_image.h"


static void encode_u16_be(
    uint8_t *buffer,
    uint16_t value)
{
    buffer[0] =
        (uint8_t)((value >> 8U) & 0xFFU);

    buffer[1] =
        (uint8_t)(value & 0xFFU);
}


static uint16_t decode_u16_be(
    const uint8_t *buffer)
{
    return
        (uint16_t)(
            ((uint16_t)buffer[0] << 8U) |
            ((uint16_t)buffer[1])
        );
}


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


esp_err_t sgp_image_encode_meta(
    uint8_t *buffer,
    size_t buffer_len,
    const sgp_image_meta_t *meta)
{
    if ((buffer == NULL) ||
        (meta == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (buffer_len < SGP_IMAGE_META_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    encode_u32_be(
        &buffer[0],
        meta->image_id
    );


    encode_u32_be(
        &buffer[4],
        meta->total_size
    );


    encode_u16_be(
        &buffer[8],
        meta->chunk_count
    );


    encode_u16_be(
        &buffer[10],
        meta->width
    );


    encode_u16_be(
        &buffer[12],
        meta->height
    );


    buffer[14] =
        meta->format;

    buffer[15] =
        meta->quality;


    return ESP_OK;
}


esp_err_t sgp_image_decode_meta(
    const uint8_t *buffer,
    size_t buffer_len,
    sgp_image_meta_t *meta)
{
    if ((buffer == NULL) ||
        (meta == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (buffer_len != SGP_IMAGE_META_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    meta->image_id =
        decode_u32_be(&buffer[0]);

    meta->total_size =
        decode_u32_be(&buffer[4]);

    meta->chunk_count =
        decode_u16_be(&buffer[8]);

    meta->width =
        decode_u16_be(&buffer[10]);

    meta->height =
        decode_u16_be(&buffer[12]);

    meta->format =
        buffer[14];

    meta->quality =
        buffer[15];


    return ESP_OK;
}


esp_err_t sgp_image_encode_chunk_header(
    uint8_t *buffer,
    size_t buffer_len,
    const sgp_image_chunk_header_t *header)
{
    if ((buffer == NULL) ||
        (header == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (buffer_len < SGP_IMAGE_CHUNK_HEADER_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    if (header->data_len >
        SGP_IMAGE_CHUNK_DATA_MAX)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    encode_u32_be(
        &buffer[0],
        header->image_id
    );


    encode_u16_be(
        &buffer[4],
        header->chunk_index
    );


    encode_u16_be(
        &buffer[6],
        header->data_len
    );


    return ESP_OK;
}


esp_err_t sgp_image_decode_chunk_header(
    const uint8_t *buffer,
    size_t buffer_len,
    sgp_image_chunk_header_t *header)
{
    if ((buffer == NULL) ||
        (header == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (buffer_len <
        SGP_IMAGE_CHUNK_HEADER_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    header->image_id =
        decode_u32_be(&buffer[0]);

    header->chunk_index =
        decode_u16_be(&buffer[4]);

    header->data_len =
        decode_u16_be(&buffer[6]);


    if (header->data_len >
        SGP_IMAGE_CHUNK_DATA_MAX)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    return ESP_OK;
}


esp_err_t sgp_image_encode_done(
    uint8_t *buffer,
    size_t buffer_len,
    const sgp_image_done_t *done)
{
    if ((buffer == NULL) ||
        (done == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (buffer_len < SGP_IMAGE_DONE_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    encode_u32_be(
        &buffer[0],
        done->image_id
    );


    encode_u32_be(
        &buffer[4],
        done->total_size
    );


    return ESP_OK;
}


esp_err_t sgp_image_decode_done(
    const uint8_t *buffer,
    size_t buffer_len,
    sgp_image_done_t *done)
{
    if ((buffer == NULL) ||
        (done == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (buffer_len != SGP_IMAGE_DONE_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    done->image_id =
        decode_u32_be(&buffer[0]);

    done->total_size =
        decode_u32_be(&buffer[4]);


    return ESP_OK;
}