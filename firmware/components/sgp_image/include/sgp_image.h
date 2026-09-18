#ifndef SGP_IMAGE_H
#define SGP_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"


#define SGP_MSG_IMAGE_META              0x20U
#define SGP_MSG_IMAGE_CHUNK             0x21U
#define SGP_MSG_IMAGE_DONE              0x22U

#define SGP_IMAGE_FORMAT_JPEG           0x01U

#define SGP_IMAGE_META_SIZE             16U
#define SGP_IMAGE_CHUNK_HEADER_SIZE     8U
#define SGP_IMAGE_CHUNK_DATA_MAX        200U

#define SGP_IMAGE_CHUNK_PAYLOAD_MAX \
    (SGP_IMAGE_CHUNK_HEADER_SIZE + SGP_IMAGE_CHUNK_DATA_MAX)

#define SGP_IMAGE_DONE_SIZE             8U

#define SGP_IMAGE_MAX_SIZE              (256U * 1024U)


typedef struct
{
    uint32_t image_id;
    uint32_t total_size;

    uint16_t chunk_count;
    uint16_t width;
    uint16_t height;

    uint8_t format;
    uint8_t quality;

} sgp_image_meta_t;


typedef struct
{
    uint32_t image_id;

    uint16_t chunk_index;
    uint16_t data_len;

} sgp_image_chunk_header_t;


typedef struct
{
    uint32_t image_id;
    uint32_t total_size;

} sgp_image_done_t;


esp_err_t sgp_image_encode_meta(
    uint8_t *buffer,
    size_t buffer_len,
    const sgp_image_meta_t *meta
);


esp_err_t sgp_image_decode_meta(
    const uint8_t *buffer,
    size_t buffer_len,
    sgp_image_meta_t *meta
);


esp_err_t sgp_image_encode_chunk_header(
    uint8_t *buffer,
    size_t buffer_len,
    const sgp_image_chunk_header_t *header
);


esp_err_t sgp_image_decode_chunk_header(
    const uint8_t *buffer,
    size_t buffer_len,
    sgp_image_chunk_header_t *header
);


esp_err_t sgp_image_encode_done(
    uint8_t *buffer,
    size_t buffer_len,
    const sgp_image_done_t *done
);


esp_err_t sgp_image_decode_done(
    const uint8_t *buffer,
    size_t buffer_len,
    sgp_image_done_t *done
);


#endif