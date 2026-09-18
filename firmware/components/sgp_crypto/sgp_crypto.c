#include "sgp_crypto.h"
#include "sgp_secrets.h"
#include <string.h>


#define SGP_KDF_MASTER_KEY_SIZE \
    SGP_SECRET_KEY_SIZE

#define SGP_HKDF_ALGORITHM \
    PSA_ALG_HKDF(PSA_ALG_SHA_256)


/*
 * Development encryption/KDF master key.
 */
static const uint8_t sgp_kdf_master_key[
    SGP_KDF_MASTER_KEY_SIZE
] = SGP_KDF_MASTER_KEY_BYTES;


static void encode_u32_be(
    uint8_t *buffer,
    uint32_t value)
{
    buffer[0] = (uint8_t)((value >> 24U) & 0xFFU);
    buffer[1] = (uint8_t)((value >> 16U) & 0xFFU);
    buffer[2] = (uint8_t)((value >> 8U) & 0xFFU);
    buffer[3] = (uint8_t)(value & 0xFFU);
}


static void build_nonce(
    uint32_t session_id,
    uint32_t sequence,
    uint8_t direction,
    uint8_t message_type,
    uint8_t nonce[SGP_GCM_NONCE_SIZE])
{
    memset(
        nonce,
        0,
        SGP_GCM_NONCE_SIZE
    );


    encode_u32_be(
        &nonce[0],
        session_id
    );


    encode_u32_be(
        &nonce[4],
        sequence
    );


    nonce[8] =
        direction;

    nonce[9] =
        message_type;
}


static esp_err_t import_aes_key(
    const uint8_t *key,
    psa_key_id_t *key_id)
{
    psa_key_attributes_t attributes =
        PSA_KEY_ATTRIBUTES_INIT;


    psa_set_key_type(
        &attributes,
        PSA_KEY_TYPE_AES
    );


    psa_set_key_bits(
        &attributes,
        SGP_AES_KEY_SIZE * 8U
    );


    psa_set_key_usage_flags(
        &attributes,
        PSA_KEY_USAGE_ENCRYPT |
        PSA_KEY_USAGE_DECRYPT
    );


    psa_set_key_algorithm(
        &attributes,
        PSA_ALG_GCM
    );


    const psa_status_t status =
        psa_import_key(
            &attributes,
            key,
            SGP_AES_KEY_SIZE,
            key_id
        );


    psa_reset_key_attributes(
        &attributes
    );


    if (status != PSA_SUCCESS)
    {
        return ESP_FAIL;
    }


    return ESP_OK;
}


esp_err_t sgp_crypto_session_init(
    uint32_t session_id,
    sgp_crypto_session_t *session)
{
    if ((session == NULL) ||
        (session_id == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }


    sgp_crypto_session_deinit(
        session
    );


    if (psa_crypto_init() != PSA_SUCCESS)
    {
        return ESP_FAIL;
    }


    psa_key_id_t master_key_id = 0;


    psa_key_attributes_t attributes =
        PSA_KEY_ATTRIBUTES_INIT;


    psa_set_key_type(
        &attributes,
        PSA_KEY_TYPE_DERIVE
    );


    psa_set_key_bits(
        &attributes,
        SGP_KDF_MASTER_KEY_SIZE * 8U
    );


    psa_set_key_usage_flags(
        &attributes,
        PSA_KEY_USAGE_DERIVE
    );


    psa_set_key_algorithm(
        &attributes,
        SGP_HKDF_ALGORITHM
    );


    psa_status_t status =
        psa_import_key(
            &attributes,
            sgp_kdf_master_key,
            sizeof(sgp_kdf_master_key),
            &master_key_id
        );


    psa_reset_key_attributes(
        &attributes
    );


    if (status != PSA_SUCCESS)
    {
        return ESP_FAIL;
    }


    uint8_t salt[8] =
    {
        'S', 'G', 'P', '1',
        0, 0, 0, 0
    };


    encode_u32_be(
        &salt[4],
        session_id
    );


    static const uint8_t info[] =
        "SGP-v1-AES-GCM-session";


    uint8_t derived_material[
        SGP_AES_KEY_SIZE * 2U
    ] = {0};


    psa_key_derivation_operation_t operation =
        PSA_KEY_DERIVATION_OPERATION_INIT;


    status =
        psa_key_derivation_setup(
            &operation,
            SGP_HKDF_ALGORITHM
        );


    if (status == PSA_SUCCESS)
    {
        status =
            psa_key_derivation_input_bytes(
                &operation,
                PSA_KEY_DERIVATION_INPUT_SALT,
                salt,
                sizeof(salt)
            );
    }


    if (status == PSA_SUCCESS)
    {
        status =
            psa_key_derivation_input_key(
                &operation,
                PSA_KEY_DERIVATION_INPUT_SECRET,
                master_key_id
            );
    }


    if (status == PSA_SUCCESS)
    {
        status =
            psa_key_derivation_input_bytes(
                &operation,
                PSA_KEY_DERIVATION_INPUT_INFO,
                info,
                sizeof(info) - 1U
            );
    }


    if (status == PSA_SUCCESS)
    {
        status =
            psa_key_derivation_output_bytes(
                &operation,
                derived_material,
                sizeof(derived_material)
            );
    }


    (void)psa_key_derivation_abort(
        &operation
    );


    (void)psa_destroy_key(
        master_key_id
    );


    if (status != PSA_SUCCESS)
    {
        memset(
            derived_material,
            0,
            sizeof(derived_material)
        );

        return ESP_FAIL;
    }


    esp_err_t result =
        import_aes_key(
            &derived_material[0],
            &session->camera_to_gateway_key
        );


    if (result == ESP_OK)
    {
        result =
            import_aes_key(
                &derived_material[SGP_AES_KEY_SIZE],
                &session->gateway_to_camera_key
            );
    }


    memset(
        derived_material,
        0,
        sizeof(derived_material)
    );


    if (result != ESP_OK)
    {
        sgp_crypto_session_deinit(
            session
        );

        return result;
    }


    session->session_id =
        session_id;

    session->initialized =
        true;


    return ESP_OK;
}


void sgp_crypto_session_deinit(
    sgp_crypto_session_t *session)
{
    if (session == NULL)
    {
        return;
    }


    if (session->camera_to_gateway_key != 0)
    {
        (void)psa_destroy_key(
            session->camera_to_gateway_key
        );
    }


    if (session->gateway_to_camera_key != 0)
    {
        (void)psa_destroy_key(
            session->gateway_to_camera_key
        );
    }


    memset(
        session,
        0,
        sizeof(*session)
    );
}


static psa_key_id_t select_key(
    const sgp_crypto_session_t *session,
    uint8_t direction)
{
    if (direction == SGP_DIRECTION_C2G)
    {
        return session->camera_to_gateway_key;
    }


    if (direction == SGP_DIRECTION_G2C)
    {
        return session->gateway_to_camera_key;
    }


    return 0;
}


esp_err_t sgp_crypto_encrypt_packet(
    const sgp_crypto_session_t *session,
    uint8_t direction,
    const sgp_header_t *header,
    const uint8_t *plaintext,
    size_t plaintext_len,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_len)
{
    if ((session == NULL) ||
        (header == NULL) ||
        (plaintext == NULL) ||
        (output == NULL) ||
        (output_len == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if ((!session->initialized) ||
        (header->session_id != session->session_id))
    {
        return ESP_ERR_INVALID_STATE;
    }


    /*
     * ESP32 AES-GCM donanım yolu için
     * boş plaintext kullanmıyoruz.
     */
    if (plaintext_len == 0U)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    const size_t required_size =
        SGP_HEADER_SIZE +
        plaintext_len +
        SGP_GCM_TAG_SIZE;


    if (output_capacity < required_size)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    esp_err_t result =
        sgp_encode_header(
            output,
            SGP_HEADER_SIZE,
            header
        );


    if (result != ESP_OK)
    {
        return result;
    }


    uint8_t nonce[SGP_GCM_NONCE_SIZE];


    build_nonce(
        header->session_id,
        header->sequence,
        direction,
        header->type,
        nonce
    );


    const psa_key_id_t key_id =
        select_key(
            session,
            direction
        );


    if (key_id == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }


    size_t encrypted_len = 0U;


    const psa_status_t status =
        psa_aead_encrypt(
            key_id,
            PSA_ALG_GCM,
            nonce,
            sizeof(nonce),

            output,
            SGP_HEADER_SIZE,

            plaintext,
            plaintext_len,

            &output[SGP_HEADER_SIZE],
            output_capacity - SGP_HEADER_SIZE,
            &encrypted_len
        );


    if (status != PSA_SUCCESS)
    {
        return ESP_FAIL;
    }


    if (encrypted_len !=
        (plaintext_len + SGP_GCM_TAG_SIZE))
    {
        return ESP_FAIL;
    }


    *output_len =
        SGP_HEADER_SIZE +
        encrypted_len;


    return ESP_OK;
}


esp_err_t sgp_crypto_decrypt_packet(
    const sgp_crypto_session_t *session,
    uint8_t direction,
    const uint8_t *packet,
    size_t packet_len,
    sgp_header_t *header,
    uint8_t *plaintext,
    size_t plaintext_capacity,
    size_t *plaintext_len)
{
    if ((session == NULL) ||
        (packet == NULL) ||
        (header == NULL) ||
        (plaintext == NULL) ||
        (plaintext_len == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (!session->initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }


    if (packet_len <=
        (SGP_HEADER_SIZE + SGP_GCM_TAG_SIZE))
    {
        return ESP_ERR_INVALID_SIZE;
    }


    esp_err_t result =
        sgp_decode_header(
            packet,
            SGP_HEADER_SIZE,
            header
        );


    if (result != ESP_OK)
    {
        return result;
    }


    if (header->session_id !=
        session->session_id)
    {
        return ESP_ERR_INVALID_STATE;
    }


    const size_t encrypted_len =
        packet_len -
        SGP_HEADER_SIZE;


    const size_t expected_plaintext_len =
        encrypted_len -
        SGP_GCM_TAG_SIZE;


    if (plaintext_capacity <
        expected_plaintext_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    uint8_t nonce[SGP_GCM_NONCE_SIZE];


    build_nonce(
        header->session_id,
        header->sequence,
        direction,
        header->type,
        nonce
    );


    const psa_key_id_t key_id =
        select_key(
            session,
            direction
        );


    if (key_id == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }


    size_t decrypted_len = 0U;


    const psa_status_t status =
        psa_aead_decrypt(
            key_id,
            PSA_ALG_GCM,
            nonce,
            sizeof(nonce),

            packet,
            SGP_HEADER_SIZE,

            &packet[SGP_HEADER_SIZE],
            encrypted_len,

            plaintext,
            plaintext_capacity,
            &decrypted_len
        );


    if (status != PSA_SUCCESS)
    {
        return ESP_ERR_INVALID_CRC;
    }


    *plaintext_len =
        decrypted_len;


    return ESP_OK;
}