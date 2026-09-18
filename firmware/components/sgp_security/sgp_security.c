#include "sgp_security.h"
#include "sgp_secrets.h"
#include "psa/crypto.h"


#define SGP_PSK_SIZE SGP_SECRET_KEY_SIZE

#define SGP_HMAC_ALGORITHM \
    PSA_ALG_TRUNCATED_MAC( \
        PSA_ALG_HMAC(PSA_ALG_SHA_256), \
        SGP_HMAC_TAG_SIZE)


/*
 * Development PSK.
 */
static const uint8_t sgp_psk[
    SGP_PSK_SIZE
] = SGP_HMAC_PSK_BYTES;


static psa_key_id_t hmac_key_id = 0;


esp_err_t sgp_security_init(void)
{
    if (hmac_key_id != 0)
    {
        return ESP_OK;
    }


    psa_status_t status =
        psa_crypto_init();


    if (status != PSA_SUCCESS)
    {
        return ESP_FAIL;
    }


    psa_key_attributes_t attributes =
        PSA_KEY_ATTRIBUTES_INIT;


    psa_set_key_type(
        &attributes,
        PSA_KEY_TYPE_HMAC
    );


    psa_set_key_bits(
        &attributes,
        SGP_PSK_SIZE * 8U
    );


    psa_set_key_usage_flags(
        &attributes,
        PSA_KEY_USAGE_SIGN_MESSAGE |
        PSA_KEY_USAGE_VERIFY_MESSAGE
    );


    psa_set_key_algorithm(
        &attributes,
        SGP_HMAC_ALGORITHM
    );


    status =
        psa_import_key(
            &attributes,
            sgp_psk,
            sizeof(sgp_psk),
            &hmac_key_id
        );


    psa_reset_key_attributes(
        &attributes
    );


    if (status != PSA_SUCCESS)
    {
        hmac_key_id = 0;

        return ESP_FAIL;
    }


    return ESP_OK;
}


esp_err_t sgp_security_sign(
    const uint8_t *data,
    size_t data_len,
    uint8_t *tag,
    size_t tag_size)
{
    if ((data == NULL) ||
        (tag == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (tag_size < SGP_HMAC_TAG_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    if (hmac_key_id == 0)
    {
        return ESP_ERR_INVALID_STATE;
    }


    size_t mac_length = 0U;


    const psa_status_t status =
        psa_mac_compute(
            hmac_key_id,
            SGP_HMAC_ALGORITHM,
            data,
            data_len,
            tag,
            tag_size,
            &mac_length
        );


    if ((status != PSA_SUCCESS) ||
        (mac_length != SGP_HMAC_TAG_SIZE))
    {
        return ESP_FAIL;
    }


    return ESP_OK;
}


esp_err_t sgp_security_verify(
    const uint8_t *data,
    size_t data_len,
    const uint8_t *tag,
    size_t tag_size)
{
    if ((data == NULL) ||
        (tag == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }


    if (tag_size != SGP_HMAC_TAG_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }


    if (hmac_key_id == 0)
    {
        return ESP_ERR_INVALID_STATE;
    }


    const psa_status_t status =
        psa_mac_verify(
            hmac_key_id,
            SGP_HMAC_ALGORITHM,
            data,
            data_len,
            tag,
            tag_size
        );


    if (status != PSA_SUCCESS)
    {
        return ESP_ERR_INVALID_CRC;
    }


    return ESP_OK;
}