#ifndef SGP_PROTOCOL_H
#define SGP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"


#define SGP_VERSION             0x01U

#define SGP_MSG_HELLO           0x01U
#define SGP_MSG_ACK             0x03U

#define SGP_MSG_HEARTBEAT       0x10U
#define SGP_MSG_SECURE_ACK      0x11U

#define SGP_FLAG_NONE           0x00U

#define SGP_HEADER_SIZE         11U
#define SGP_MAX_PACKET_SIZE     250U


typedef struct
{
    uint8_t version;
    uint8_t type;
    uint8_t flags;

    uint32_t session_id;
    uint32_t sequence;

} sgp_header_t;


esp_err_t sgp_encode_header(
    uint8_t *buffer,
    size_t buffer_len,
    const sgp_header_t *header
);


esp_err_t sgp_decode_header(
    const uint8_t *buffer,
    size_t buffer_len,
    sgp_header_t *header
);


#endif