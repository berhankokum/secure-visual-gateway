#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>


#include "esp_system.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"

#include "esp_camera.h"

#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "sgp_device_config.h"
#include "sgp_protocol.h"
#include "sgp_security.h"
#include "sgp_crypto.h"
#include "sgp_image.h"
#include "sgp_telemetry.h"


static const char *TAG = "CAMERA_NODE";


#define SGP_HMAC_PACKET_SIZE \
    (SGP_HEADER_SIZE + SGP_HMAC_TAG_SIZE)


/*
 * AI-Thinker ESP32-CAM pin mapping
 */
#define CAM_PIN_PWDN            32
#define CAM_PIN_RESET           -1
#define CAM_PIN_XCLK            0
#define CAM_PIN_SIOD            26
#define CAM_PIN_SIOC            27

#define CAM_PIN_D7              35
#define CAM_PIN_D6              34
#define CAM_PIN_D5              39
#define CAM_PIN_D4              36
#define CAM_PIN_D3              21
#define CAM_PIN_D2              19
#define CAM_PIN_D1              18
#define CAM_PIN_D0              5

#define CAM_PIN_VSYNC           25
#define CAM_PIN_HREF            23
#define CAM_PIN_PCLK            22

static uint32_t images_sent = 0U;

static uint32_t capture_failures = 0U;

static uint32_t transfer_failures = 0U;

static const uint8_t gateway_mac[
    ESP_NOW_ETH_ALEN
] = SGP_GATEWAY_MAC_BYTES;


typedef struct
{
    uint8_t type;
    uint32_t sequence;

} ack_event_t;


static QueueHandle_t ack_queue = NULL;

static uint32_t camera_session_id = 0U;

static sgp_crypto_session_t crypto_session = {0};


static void espnow_send_callback(
    const esp_now_send_info_t *tx_info,
    esp_now_send_status_t status)
{
    if ((tx_info != NULL) &&
        (status != ESP_NOW_SEND_SUCCESS))
    {
        ESP_LOGW(
            TAG,
            "ESP-NOW MAC delivery failed"
        );
    }
}


static void push_ack_event(
    uint8_t type,
    uint32_t sequence)
{
    if (ack_queue == NULL)
    {
        return;
    }


    const ack_event_t event =
    {
        .type = type,
        .sequence = sequence
    };


    (void)xQueueSend(
        ack_queue,
        &event,
        0
    );
}


static void espnow_receive_callback(
    const esp_now_recv_info_t *recv_info,
    const uint8_t *data,
    int data_len)
{
    if ((recv_info == NULL) ||
        (data == NULL))
    {
        return;
    }


    if (memcmp(
            recv_info->src_addr,
            gateway_mac,
            ESP_NOW_ETH_ALEN) != 0)
    {
        return;
    }


    /*
     * Handshake ACK
     */
    if (data_len == SGP_HMAC_PACKET_SIZE)
    {
        const uint8_t *tag =
            &data[SGP_HEADER_SIZE];


        if (sgp_security_verify(
                data,
                SGP_HEADER_SIZE,
                tag,
                SGP_HMAC_TAG_SIZE) != ESP_OK)
        {
            return;
        }


        sgp_header_t header = {0};


        if (sgp_decode_header(
                data,
                SGP_HEADER_SIZE,
                &header) != ESP_OK)
        {
            return;
        }


        if ((header.version == SGP_VERSION) &&
            (header.type == SGP_MSG_ACK) &&
            (header.session_id == camera_session_id))
        {
            push_ack_event(
                SGP_MSG_ACK,
                header.sequence
            );
        }


        return;
    }


    /*
     * Encrypted ACK
     */
    uint8_t plaintext[1];

    size_t plaintext_len = 0U;

    sgp_header_t header = {0};


    if (sgp_crypto_decrypt_packet(
            &crypto_session,
            SGP_DIRECTION_G2C,
            data,
            (size_t)data_len,
            &header,
            plaintext,
            sizeof(plaintext),
            &plaintext_len) != ESP_OK)
    {
        return;
    }


    if ((header.version != SGP_VERSION) ||
        (header.type != SGP_MSG_SECURE_ACK) ||
        (header.session_id != camera_session_id) ||
        (plaintext_len != 1U) ||
        (plaintext[0] != 0xACU))
    {
        return;
    }


    push_ack_event(
        SGP_MSG_SECURE_ACK,
        header.sequence
    );
}


static esp_err_t camera_init(void)
{
    const camera_config_t config =
    {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,

        .pin_xclk = CAM_PIN_XCLK,

        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,

        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        .xclk_freq_hz = 20000000,

        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        /*
         * GC2145 doğrudan JPEG üretmiyor.
         * Önce RGB565 capture edeceğiz.
         */
        .pixel_format = PIXFORMAT_RGB565,

        .frame_size = FRAMESIZE_QVGA,

        /*
         * RGB565 capture'da bu değer kamera sensörü
         * tarafından kullanılmaz. JPEG kalitesini
         * frame2jpg() aşamasında belirleyeceğiz.
         */
        .jpeg_quality = 12,

        .fb_count = 1,

        .fb_location = CAMERA_FB_IN_PSRAM,

        .grab_mode = CAMERA_GRAB_WHEN_EMPTY
    };


    return esp_camera_init(
        &config
    );
}


static void wifi_init(void)
{
    ESP_ERROR_CHECK(
        esp_netif_init()
    );


    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );


    wifi_init_config_t config =
        WIFI_INIT_CONFIG_DEFAULT();


    ESP_ERROR_CHECK(
        esp_wifi_init(&config)
    );


    ESP_ERROR_CHECK(
        esp_wifi_set_storage(
            WIFI_STORAGE_RAM
        )
    );


    ESP_ERROR_CHECK(
        esp_wifi_set_mode(
            WIFI_MODE_STA
        )
    );


    ESP_ERROR_CHECK(
        esp_wifi_start()
    );


    ESP_ERROR_CHECK(
        esp_wifi_set_channel(
            SGP_ESPNOW_CHANNEL,
            WIFI_SECOND_CHAN_NONE
        )
    );
}


static void espnow_init(void)
{
    ESP_ERROR_CHECK(
        esp_now_init()
    );


    ESP_ERROR_CHECK(
        esp_now_register_send_cb(
            espnow_send_callback
        )
    );


    ESP_ERROR_CHECK(
        esp_now_register_recv_cb(
            espnow_receive_callback
        )
    );


    esp_now_peer_info_t peer = {0};


    memcpy(
        peer.peer_addr,
        gateway_mac,
        ESP_NOW_ETH_ALEN
    );


    peer.channel = SGP_ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;


    ESP_ERROR_CHECK(
        esp_now_add_peer(&peer)
    );
}


static void generate_session_id(void)
{
    do
    {
        camera_session_id =
            esp_random();

    } while (camera_session_id == 0U);


    ESP_LOGI(
        TAG,
        "Session ID: 0x%08lX",
        (unsigned long)camera_session_id
    );
}


static void clear_ack_queue(void)
{
    ack_event_t event;


    while (xQueueReceive(
               ack_queue,
               &event,
               0) == pdTRUE)
    {
    }
}


static bool wait_for_ack(
    uint8_t expected_type,
    uint32_t expected_sequence)
{
    ack_event_t event = {0};


    if (xQueueReceive(
            ack_queue,
            &event,
            pdMS_TO_TICKS(
                SGP_ACK_TIMEOUT_MS
            )) != pdTRUE)
    {
        return false;
    }


    return
        (event.type == expected_type) &&
        (event.sequence == expected_sequence);
}


static esp_err_t send_hello(void)
{
    uint8_t packet[SGP_HMAC_PACKET_SIZE] = {0};


    const sgp_header_t header =
    {
        .version = SGP_VERSION,
        .type = SGP_MSG_HELLO,
        .flags = SGP_FLAG_NONE,
        .session_id = camera_session_id,
        .sequence = 1U
    };


    ESP_RETURN_ON_ERROR(
        sgp_encode_header(
            packet,
            SGP_HEADER_SIZE,
            &header
        ),
        TAG,
        "HELLO encode failed"
    );


    ESP_RETURN_ON_ERROR(
        sgp_security_sign(
            packet,
            SGP_HEADER_SIZE,
            &packet[SGP_HEADER_SIZE],
            SGP_HMAC_TAG_SIZE
        ),
        TAG,
        "HELLO HMAC failed"
    );


    for (uint32_t retry = 0U;
         retry < SGP_MAX_RETRY_COUNT;
         retry++)
    {
        clear_ack_queue();


        if (esp_now_send(
                gateway_mac,
                packet,
                sizeof(packet)) != ESP_OK)
        {
            continue;
        }


        if (wait_for_ack(
                SGP_MSG_ACK,
                1U))
        {
            return ESP_OK;
        }
    }


    return ESP_ERR_TIMEOUT;
}


static esp_err_t secure_send_reliable(
    uint8_t message_type,
    const uint8_t *payload,
    size_t payload_len,
    uint32_t *sequence)
{
    if ((payload == NULL) ||
        (sequence == NULL) ||
        (payload_len == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }


    uint8_t packet[SGP_MAX_PACKET_SIZE];

    size_t packet_len = 0U;


    const sgp_header_t header =
    {
        .version = SGP_VERSION,
        .type = message_type,
        .flags = SGP_FLAG_NONE,
        .session_id = camera_session_id,
        .sequence = *sequence
    };


    ESP_RETURN_ON_ERROR(
        sgp_crypto_encrypt_packet(
            &crypto_session,
            SGP_DIRECTION_C2G,
            &header,
            payload,
            payload_len,
            packet,
            sizeof(packet),
            &packet_len
        ),
        TAG,
        "Packet encryption failed"
    );


    for (uint32_t retry = 0U;
         retry < SGP_MAX_RETRY_COUNT;
         retry++)
    {
        clear_ack_queue();


        if (esp_now_send(
                gateway_mac,
                packet,
                packet_len) != ESP_OK)
        {
            continue;
        }


        if (wait_for_ack(
                SGP_MSG_SECURE_ACK,
                *sequence))
        {
            (*sequence)++;

            return ESP_OK;
        }


        ESP_LOGW(
            TAG,
            "Secure ACK timeout seq=%lu retry=%lu",
            (unsigned long)*sequence,
            (unsigned long)(retry + 1U)
        );
    }


    return ESP_ERR_TIMEOUT;
}

static esp_err_t send_telemetry(
    uint32_t *secure_sequence)
{
    uint8_t payload[
        SGP_TELEMETRY_SIZE
    ];


    const uint64_t uptime_us =
        (uint64_t)esp_timer_get_time();


    const sgp_telemetry_t telemetry =
    {
        .reset_reason =
            (uint8_t)esp_reset_reason(),

        .uptime_ms =
            (uint32_t)(
                uptime_us / 1000ULL
            ),

        .free_heap =
            esp_get_free_heap_size(),

        .min_free_heap =
            esp_get_minimum_free_heap_size(),

        .images_sent =
            images_sent,

        .capture_failures =
            capture_failures,

        .transfer_failures =
            transfer_failures
    };


    ESP_RETURN_ON_ERROR(
        sgp_telemetry_encode(
            payload,
            sizeof(payload),
            &telemetry
        ),
        TAG,
        "Telemetry encode failed"
    );


    return secure_send_reliable(
        SGP_MSG_TELEMETRY,
        payload,
        sizeof(payload),
        secure_sequence
    );
}

static esp_err_t transmit_image(
    uint32_t image_id,
    uint32_t *secure_sequence)
{
    camera_fb_t *fb =
        esp_camera_fb_get();


    if (fb == NULL)
    {
        ESP_LOGE(
            TAG,
            "Camera capture failed"
        );
        capture_failures++;            
        return ESP_FAIL;
    }


    esp_err_t result =
        ESP_FAIL;


    uint8_t *jpeg_buf =
        NULL;

    size_t jpeg_len =
        0U;

    bool jpeg_buffer_allocated =
        false;


    /*
     * GC2145 bize RGB565 frame veriyor.
     *
     * frame2jpg() bunu yazılımsal olarak JPEG'e
     * dönüştürüp yeni bir buffer allocate ediyor.
     */
    if (fb->format == PIXFORMAT_JPEG)
    {
        jpeg_buf =
            fb->buf;

        jpeg_len =
            fb->len;
    }
    else
    {
        ESP_LOGI(
            TAG,
            "Converting frame to JPEG..."
        );


        const bool conversion_ok =
            frame2jpg(
                fb,
                SGP_SOFTWARE_JPEG_QUALITY,
                &jpeg_buf,
                &jpeg_len
            );


        if ((!conversion_ok) ||
            (jpeg_buf == NULL) ||
            (jpeg_len == 0U))
        {
            ESP_LOGE(
                TAG,
                "JPEG conversion failed"
            );

            goto cleanup;
        }


        jpeg_buffer_allocated =
            true;
    }


    if (jpeg_len >
        SGP_IMAGE_MAX_SIZE)
    {
        ESP_LOGE(
            TAG,
            "JPEG too large: %u bytes",
            (unsigned)jpeg_len
        );

        goto cleanup;
    }


    const size_t chunk_count_size =
        (jpeg_len +
         SGP_IMAGE_CHUNK_DATA_MAX - 1U) /
        SGP_IMAGE_CHUNK_DATA_MAX;


    if ((chunk_count_size == 0U) ||
        (chunk_count_size > UINT16_MAX))
    {
        ESP_LOGE(
            TAG,
            "Invalid image chunk count"
        );

        goto cleanup;
    }


    const uint16_t chunk_count =
        (uint16_t)chunk_count_size;


    ESP_LOGI(
        TAG,
        "Captured image id=%lu RGB565=%u bytes JPEG=%u bytes resolution=%ux%u chunks=%u",
        (unsigned long)image_id,
        (unsigned)fb->len,
        (unsigned)jpeg_len,
        (unsigned)fb->width,
        (unsigned)fb->height,
        chunk_count
    );


    /*
     * ========================================================
     * IMAGE_META
     * ========================================================
     */

    uint8_t meta_payload[
        SGP_IMAGE_META_SIZE
    ];


    const sgp_image_meta_t meta =
    {
        .image_id =
            image_id,

        .total_size =
            (uint32_t)jpeg_len,

        .chunk_count =
            chunk_count,

        .width =
            (uint16_t)fb->width,

        .height =
            (uint16_t)fb->height,

        .format =
            SGP_IMAGE_FORMAT_JPEG,

        .quality =
            SGP_SOFTWARE_JPEG_QUALITY
    };


    result =
        sgp_image_encode_meta(
            meta_payload,
            sizeof(meta_payload),
            &meta
        );


    if (result != ESP_OK)
    {
        goto cleanup;
    }


    result =
        secure_send_reliable(
            SGP_MSG_IMAGE_META,
            meta_payload,
            sizeof(meta_payload),
            secure_sequence
        );


    if (result != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "IMAGE_META transfer failed"
        );

        goto cleanup;
    }


    /*
     * ========================================================
     * IMAGE_CHUNK
     * ========================================================
     */

    for (uint16_t chunk_index = 0U;
         chunk_index < chunk_count;
         chunk_index++)
    {
        const size_t offset =
            (size_t)chunk_index *
            SGP_IMAGE_CHUNK_DATA_MAX;


        const size_t remaining =
            jpeg_len -
            offset;


        const uint16_t data_len =
            (uint16_t)(
                (remaining >
                 SGP_IMAGE_CHUNK_DATA_MAX)
                    ? SGP_IMAGE_CHUNK_DATA_MAX
                    : remaining
            );


        uint8_t chunk_payload[
            SGP_IMAGE_CHUNK_PAYLOAD_MAX
        ];


        const sgp_image_chunk_header_t
            chunk_header =
        {
            .image_id =
                image_id,

            .chunk_index =
                chunk_index,

            .data_len =
                data_len
        };


        result =
            sgp_image_encode_chunk_header(
                chunk_payload,
                sizeof(chunk_payload),
                &chunk_header
            );


        if (result != ESP_OK)
        {
            goto cleanup;
        }


        memcpy(
            &chunk_payload[
                SGP_IMAGE_CHUNK_HEADER_SIZE
            ],
            &jpeg_buf[offset],
            data_len
        );


        result =
            secure_send_reliable(
                SGP_MSG_IMAGE_CHUNK,
                chunk_payload,
                SGP_IMAGE_CHUNK_HEADER_SIZE +
                    data_len,
                secure_sequence
            );


        if (result != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Chunk transfer failed index=%u",
                chunk_index
            );

            goto cleanup;
        }
    }


    /*
     * ========================================================
     * IMAGE_DONE
     * ========================================================
     */

    uint8_t done_payload[
        SGP_IMAGE_DONE_SIZE
    ];


    const sgp_image_done_t done =
    {
        .image_id =
            image_id,

        .total_size =
            (uint32_t)jpeg_len
    };


    result =
        sgp_image_encode_done(
            done_payload,
            sizeof(done_payload),
            &done
        );


    if (result != ESP_OK)
    {
        goto cleanup;
    }


    result =
        secure_send_reliable(
            SGP_MSG_IMAGE_DONE,
            done_payload,
            sizeof(done_payload),
            secure_sequence
        );


    if (result == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "IMAGE transfer complete id=%lu JPEG=%u bytes",
            (unsigned long)image_id,
            (unsigned)jpeg_len
        );
    }


cleanup:

    /*
     * frame2jpg() JPEG buffer'ı kendisi allocate eder.
     * Kullanıcı free etmek zorunda.
     */
    if (jpeg_buffer_allocated &&
        (jpeg_buf != NULL))
    {
        free(
            jpeg_buf
        );

        jpeg_buf =
            NULL;
    }


    esp_camera_fb_return(
        fb
    );


    return result;
}


static void communication_task(
    void *arg)
{
    (void)arg;


    while (send_hello() != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Handshake failed, retrying"
        );


        vTaskDelay(
            pdMS_TO_TICKS(2000)
        );
    }


    ESP_LOGI(
        TAG,
        "Secure session established"
    );


    uint32_t secure_sequence = 1U;

    uint32_t image_id = 1U;


    while (1)
    {
        const esp_err_t result =
            transmit_image(
                image_id,
                &secure_sequence
            );

        if (result == ESP_OK)
        {
            images_sent++;

            image_id++;
        }
        else
        {
            transfer_failures++;

            ESP_LOGE(
                TAG,
                "Image transfer failed: %s",
                esp_err_to_name(result));
        }

        const esp_err_t telemetry_result =
            send_telemetry(
                &secure_sequence);

        if (telemetry_result != ESP_OK)
        {
            ESP_LOGW(
                TAG,
                "Telemetry transfer failed: %s",
                esp_err_to_name(
                    telemetry_result));
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                SGP_IMAGE_INTERVAL_MS));
    }
}

void app_main(void)
{
    uint8_t mac[6];


    esp_err_t nvs_result =
        nvs_flash_init();


    if ((nvs_result ==
         ESP_ERR_NVS_NO_FREE_PAGES) ||
        (nvs_result ==
         ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );


        ESP_ERROR_CHECK(
            nvs_flash_init()
        );
    }
    else
    {
        ESP_ERROR_CHECK(
            nvs_result
        );
    }


    printf("\n");
    printf("==============================\n");
    printf(" Secure Visual Gateway\n");
    printf(" Role: CAMERA_NODE\n");
    printf(" Firmware: 0.13.0\n");
    printf("==============================\n");


    ESP_ERROR_CHECK(
        sgp_security_init()
    );


    generate_session_id();


    ESP_ERROR_CHECK(
        sgp_crypto_session_init(
            camera_session_id,
            &crypto_session
        )
    );


    ESP_ERROR_CHECK(
        camera_init()
    );


    ESP_LOGI(
        TAG,
        "Camera initialized"
    );


    ack_queue =
        xQueueCreate(
            4,
            sizeof(ack_event_t)
        );


    if (ack_queue == NULL)
    {
        ESP_LOGE(
            TAG,
            "ACK queue creation failed"
        );

        return;
    }


    ESP_ERROR_CHECK(
        esp_read_mac(
            mac,
            ESP_MAC_WIFI_STA
        )
    );


    wifi_init();

    espnow_init();


    if (xTaskCreate(
            communication_task,
            "communication_task",
            6144,
            NULL,
            5,
            NULL) != pdPASS)
    {
        ESP_LOGE(
            TAG,
            "Communication task creation failed"
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Camera node ready MAC=%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]
    );
}