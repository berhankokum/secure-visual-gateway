#ifndef SGP_CRYPTO_H
#define SGP_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "psa/crypto.h"

#include "sgp_protocol.h"


#define SGP_AES_KEY_SIZE       32U
#define SGP_GCM_TAG_SIZE       16U
#define SGP_GCM_NONCE_SIZE     12U

#define SGP_DIRECTION_C2G      0x01U
#define SGP_DIRECTION_G2C      0x02U


typedef struct
{
    uint32_t session_id;

    psa_key_id_t camera_to_gateway_key;
    psa_key_id_t gateway_to_camera_key;

    bool initialized;

} sgp_crypto_session_t;


esp_err_t sgp_crypto_session_init(
    uint32_t session_id,
    sgp_crypto_session_t *session
);


void sgp_crypto_session_deinit(
    sgp_crypto_session_t *session
);


esp_err_t sgp_crypto_encrypt_packet(
    const sgp_crypto_session_t *session,
    uint8_t direction,
    const sgp_header_t *header,
    const uint8_t *plaintext,
    size_t plaintext_len,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_len
);


esp_err_t sgp_crypto_decrypt_packet(
    const sgp_crypto_session_t *session,
    uint8_t direction,
    const uint8_t *packet,
    size_t packet_len,
    sgp_header_t *header,
    uint8_t *plaintext,
    size_t plaintext_capacity,
    size_t *plaintext_len
);


#endif