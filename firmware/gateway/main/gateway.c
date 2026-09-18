#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"

#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "sgp_device_config.h"
#include "sgp_protocol.h"
#include "sgp_security.h"
#include "sgp_crypto.h"
#include "sgp_image.h"
#include "sgp_serial_export.h"
#include "sgp_telemetry.h"

static const char *TAG = "GATEWAY";

#define RX_QUEUE_LENGTH            10U

#define SGP_HMAC_PACKET_SIZE \
    (SGP_HEADER_SIZE + SGP_HMAC_TAG_SIZE)

#define SECURE_ACK_VALUE           0xACU


static const uint8_t camera_node_mac[
    ESP_NOW_ETH_ALEN
] = SGP_CAMERA_NODE_MAC_BYTES;


typedef struct
{
    uint8_t source_mac[ESP_NOW_ETH_ALEN];

    uint8_t data[SGP_MAX_PACKET_SIZE];

    int data_len;

} gateway_rx_message_t;


typedef struct
{
    bool active;
    bool complete;

    uint32_t image_id;
    uint32_t total_size;

    uint16_t chunk_count;
    uint16_t next_chunk_index;

    uint16_t width;
    uint16_t height;

    uint8_t format;
    uint8_t quality;

    size_t received_bytes;

    uint8_t *buffer;

} image_rx_state_t;


static QueueHandle_t rx_queue = NULL;

static bool has_active_session = false;

static uint32_t active_session_id = 0U;

static uint32_t last_secure_sequence = 0U;

static sgp_crypto_session_t crypto_session =
    {0};

static image_rx_state_t image_state =
    {0};


static void reset_image_state(void)
{
    if (image_state.buffer != NULL)
    {
        heap_caps_free(
            image_state.buffer
        );
    }


    memset(
        &image_state,
        0,
        sizeof(image_state)
    );
}


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


static void send_hmac_ack(
    const uint8_t *destination_mac,
    uint32_t session_id,
    uint32_t sequence)
{
    uint8_t packet[
        SGP_HMAC_PACKET_SIZE
    ] = {0};


    const sgp_header_t header =
    {
        .version = SGP_VERSION,
        .type = SGP_MSG_ACK,
        .flags = SGP_FLAG_NONE,
        .session_id = session_id,
        .sequence = sequence
    };


    if (sgp_encode_header(
            packet,
            SGP_HEADER_SIZE,
            &header) != ESP_OK)
    {
        return;
    }


    if (sgp_security_sign(
            packet,
            SGP_HEADER_SIZE,
            &packet[SGP_HEADER_SIZE],
            SGP_HMAC_TAG_SIZE) != ESP_OK)
    {
        return;
    }


    (void)esp_now_send(
        destination_mac,
        packet,
        sizeof(packet)
    );
}


static void send_secure_ack(
    const uint8_t *destination_mac,
    uint32_t sequence)
{
    uint8_t packet[
        SGP_MAX_PACKET_SIZE
    ];


    size_t packet_len = 0U;


    const sgp_header_t header =
    {
        .version = SGP_VERSION,
        .type = SGP_MSG_SECURE_ACK,
        .flags = SGP_FLAG_NONE,
        .session_id = active_session_id,
        .sequence = sequence
    };


    const uint8_t payload[1] =
    {
        SECURE_ACK_VALUE
    };


    if (sgp_crypto_encrypt_packet(
            &crypto_session,
            SGP_DIRECTION_G2C,
            &header,
            payload,
            sizeof(payload),
            packet,
            sizeof(packet),
            &packet_len) != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Secure ACK encryption failed"
        );

        return;
    }


    (void)esp_now_send(
        destination_mac,
        packet,
        packet_len
    );
}


static void espnow_receive_callback(
    const esp_now_recv_info_t *recv_info,
    const uint8_t *data,
    int data_len)
{
    if ((recv_info == NULL) ||
        (data == NULL) ||
        (data_len <= 0) ||
        (data_len >
         SGP_MAX_PACKET_SIZE) ||
        (rx_queue == NULL))
    {
        return;
    }


    gateway_rx_message_t message =
        {0};


    memcpy(
        message.source_mac,
        recv_info->src_addr,
        ESP_NOW_ETH_ALEN
    );


    memcpy(
        message.data,
        data,
        (size_t)data_len
    );


    message.data_len =
        data_len;


    (void)xQueueSend(
        rx_queue,
        &message,
        0
    );
}


static void process_hello(
    const gateway_rx_message_t *message)
{
    if (message->data_len !=
        SGP_HMAC_PACKET_SIZE)
    {
        return;
    }


    const uint8_t *tag =
        &message->data[
            SGP_HEADER_SIZE
        ];


    if (sgp_security_verify(
            message->data,
            SGP_HEADER_SIZE,
            tag,
            SGP_HMAC_TAG_SIZE) != ESP_OK)
    {
        return;
    }


    sgp_header_t header = {0};


    if (sgp_decode_header(
            message->data,
            SGP_HEADER_SIZE,
            &header) != ESP_OK)
    {
        return;
    }


    if ((header.version !=
         SGP_VERSION) ||
        (header.type !=
         SGP_MSG_HELLO))
    {
        return;
    }


    if ((!has_active_session) ||
        (header.session_id !=
         active_session_id))
    {
        reset_image_state();


        sgp_crypto_session_deinit(
            &crypto_session
        );


        if (sgp_crypto_session_init(
                header.session_id,
                &crypto_session) != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Session key derivation failed"
            );

            return;
        }


        active_session_id =
            header.session_id;

        last_secure_sequence =
            0U;

        has_active_session =
            true;


        ESP_LOGI(
            TAG,
            "New secure session: 0x%08lX",
            (unsigned long)
                active_session_id
        );
    }


    send_hmac_ack(
        message->source_mac,
        header.session_id,
        header.sequence
    );
}


static bool process_image_meta(
    const uint8_t *payload,
    size_t payload_len)
{
    sgp_image_meta_t meta = {0};


    if (sgp_image_decode_meta(
            payload,
            payload_len,
            &meta) != ESP_OK)
    {
        return false;
    }


    if ((meta.total_size == 0U) ||
        (meta.total_size >
         SGP_IMAGE_MAX_SIZE) ||
        (meta.chunk_count == 0U) ||
        (meta.format !=
         SGP_IMAGE_FORMAT_JPEG))
    {
        return false;
    }


    const uint32_t expected_chunks =
        (meta.total_size +
         SGP_IMAGE_CHUNK_DATA_MAX - 1U) /
        SGP_IMAGE_CHUNK_DATA_MAX;


    if (expected_chunks !=
        meta.chunk_count)
    {
        return false;
    }


    reset_image_state();


    uint8_t *buffer =
        heap_caps_malloc(
            meta.total_size,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );


    if (buffer == NULL)
    {
        buffer =
            heap_caps_malloc(
                meta.total_size,
                MALLOC_CAP_8BIT
            );
    }


    if (buffer == NULL)
    {
        ESP_LOGE(
            TAG,
            "Unable to allocate %lu bytes for image",
            (unsigned long)
                meta.total_size
        );

        return false;
    }


    image_state.active = true;
    image_state.complete = false;

    image_state.image_id =
        meta.image_id;

    image_state.total_size =
        meta.total_size;

    image_state.chunk_count =
        meta.chunk_count;

    image_state.next_chunk_index =
        0U;

    image_state.width =
        meta.width;

    image_state.height =
        meta.height;

    image_state.format =
        meta.format;

    image_state.quality =
        meta.quality;

    image_state.received_bytes =
        0U;

    image_state.buffer =
        buffer;


    ESP_LOGI(
        TAG,
        "IMAGE META id=%lu size=%lu resolution=%ux%u chunks=%u",
        (unsigned long)
            image_state.image_id,
        (unsigned long)
            image_state.total_size,
        image_state.width,
        image_state.height,
        image_state.chunk_count
    );


    return true;
}


static bool process_image_chunk(
    const uint8_t *payload,
    size_t payload_len)
{
    if ((!image_state.active) ||
        (payload_len <
         SGP_IMAGE_CHUNK_HEADER_SIZE))
    {
        return false;
    }


    sgp_image_chunk_header_t chunk =
        {0};


    if (sgp_image_decode_chunk_header(
            payload,
            payload_len,
            &chunk) != ESP_OK)
    {
        return false;
    }


    if (chunk.image_id !=
        image_state.image_id)
    {
        return false;
    }


    if (chunk.chunk_index !=
        image_state.next_chunk_index)
    {
        ESP_LOGW(
            TAG,
            "Unexpected chunk index=%u expected=%u",
            chunk.chunk_index,
            image_state.next_chunk_index
        );

        return false;
    }


    if (payload_len !=
        (SGP_IMAGE_CHUNK_HEADER_SIZE +
         chunk.data_len))
    {
        return false;
    }


    if ((image_state.received_bytes +
         chunk.data_len) >
        image_state.total_size)
    {
        return false;
    }


    memcpy(
        &image_state.buffer[
            image_state.received_bytes
        ],
        &payload[
            SGP_IMAGE_CHUNK_HEADER_SIZE
        ],
        chunk.data_len
    );


    image_state.received_bytes +=
        chunk.data_len;


    image_state.next_chunk_index++;


    return true;
}


static bool process_image_done(
    const uint8_t *payload,
    size_t payload_len)
{
    if (!image_state.active)
    {
        return false;
    }


    sgp_image_done_t done = {0};


    if (sgp_image_decode_done(
            payload,
            payload_len,
            &done) != ESP_OK)
    {
        return false;
    }


    if ((done.image_id !=
         image_state.image_id) ||
        (done.total_size !=
         image_state.total_size))
    {
        return false;
    }


    if ((image_state.received_bytes !=
         image_state.total_size) ||
        (image_state.next_chunk_index !=
         image_state.chunk_count))
    {
        ESP_LOGE(
            TAG,
            "Incomplete image bytes=%u/%lu chunks=%u/%u",
            (unsigned)
                image_state.received_bytes,
            (unsigned long)
                image_state.total_size,
            image_state.next_chunk_index,
            image_state.chunk_count
        );


        return false;
    }


    if (image_state.total_size < 4U)
    {
        return false;
    }


    const bool jpeg_valid =
        (image_state.buffer[0] ==
         0xFFU) &&
        (image_state.buffer[1] ==
         0xD8U) &&
        (image_state.buffer[
            image_state.total_size - 2U
         ] == 0xFFU) &&
        (image_state.buffer[
            image_state.total_size - 1U
         ] == 0xD9U);


    if (!jpeg_valid)
    {
        ESP_LOGE(
            TAG,
            "JPEG markers invalid"
        );


        return false;
    }


    ESP_LOGI(
        TAG,
        "IMAGE COMPLETE id=%lu bytes=%lu resolution=%ux%u chunks=%u JPEG=VALID",
        (unsigned long)
            image_state.image_id,
        (unsigned long)
            image_state.total_size,
        image_state.width,
        image_state.height,
        image_state.chunk_count
    );


    /*
     * JPEG buffer sahipliğini serial export
     * task'a devrediyoruz.
     *
     * Böylece USB'ye Base64 yazılması
     * communication task'ı ve ACK gönderimini
     * bloke etmiyor.
     */
    const esp_err_t export_result =
        sgp_serial_export_submit(
        active_session_id,
        image_state.image_id,
        image_state.width,
        image_state.height,
        image_state.buffer,
        image_state.total_size
        );


    if (export_result != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Unable to queue image for serial export: %s",
            esp_err_to_name(
                export_result
            )
        );


        /*
         * IMAGE_DONE ACK gönderilmeyecek.
         * Camera aynı sequence'i retry edecek.
         */
        return false;
    }


    /*
     * Buffer artık serial export task'ın.
     *
     * reset_image_state() çağırırsak
     * buffer'ı free ederdi; bu nedenle önce
     * pointer'ı NULL yapıyoruz.
     */
    image_state.buffer =
        NULL;


    memset(
        &image_state,
        0,
        sizeof(image_state)
    );


    return true;
}


static bool process_secure_payload(
    const sgp_header_t *header,
    const uint8_t *payload,
    size_t payload_len)
{
    switch (header->type)
    {
        case SGP_MSG_IMAGE_META:

            return process_image_meta(
                payload,
                payload_len
            );


        case SGP_MSG_IMAGE_CHUNK:

            return process_image_chunk(
                payload,
                payload_len
            );


        case SGP_MSG_IMAGE_DONE:

            return process_image_done(
                payload,
                payload_len
            );

        case SGP_MSG_TELEMETRY:
        {
            sgp_telemetry_t telemetry =
                {0};

            if (sgp_telemetry_decode(
                    payload,
                    payload_len,
                    &telemetry) != ESP_OK)
            {
                return false;
            }

            printf(
                "@@SGP_STATUS "
                "session=0x%08lX "
                "reset=%u "
                "uptime=%lu "
                "free_heap=%lu "
                "min_heap=%lu "
                "images=%lu "
                "capture_failures=%lu "
                "transfer_failures=%lu\n",

                (unsigned long)
                    active_session_id,

                telemetry.reset_reason,

                (unsigned long)
                    telemetry.uptime_ms,

                (unsigned long)
                    telemetry.free_heap,

                (unsigned long)
                    telemetry.min_free_heap,

                (unsigned long)
                    telemetry.images_sent,

                (unsigned long)
                    telemetry.capture_failures,

                (unsigned long)
                    telemetry.transfer_failures);

            fflush(
                stdout);

            return true;
        }
        default:

            ESP_LOGW(
                TAG,
                "Unsupported secure message type=0x%02X",
                header->type
            );

            return false;
    }
}


static void process_secure_packet(
    const gateway_rx_message_t *message)
{
    if (!has_active_session)
    {
        return;
    }


    uint8_t plaintext[
        SGP_IMAGE_CHUNK_PAYLOAD_MAX
    ];


    size_t plaintext_len = 0U;

    sgp_header_t header = {0};


    if (sgp_crypto_decrypt_packet(
            &crypto_session,
            SGP_DIRECTION_C2G,
            message->data,
            (size_t)message->data_len,
            &header,
            plaintext,
            sizeof(plaintext),
            &plaintext_len) != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "AES-GCM authentication failed"
        );

        return;
    }


    if ((header.version !=
         SGP_VERSION) ||
        (header.session_id !=
         active_session_id))
    {
        return;
    }


    /*
     * Retry: paketi tekrar işlemiyoruz,
     * ACK'i yeniden gönderiyoruz.
     */
    if (header.sequence ==
        last_secure_sequence)
    {
        send_secure_ack(
            message->source_mac,
            header.sequence
        );

        return;
    }


    if (header.sequence <
        last_secure_sequence)
    {
        ESP_LOGW(
            TAG,
            "Stale secure packet sequence=%lu",
            (unsigned long)
                header.sequence
        );

        return;
    }


    if (header.sequence !=
        (last_secure_sequence + 1U))
    {
        ESP_LOGW(
            TAG,
            "Sequence gap expected=%lu received=%lu",
            (unsigned long)
                (last_secure_sequence + 1U),
            (unsigned long)
                header.sequence
        );

        return;
    }


    if (!process_secure_payload(
            &header,
            plaintext,
            plaintext_len))
    {
        return;
    }


    last_secure_sequence =
        header.sequence;


    send_secure_ack(
        message->source_mac,
        header.sequence
    );
}


static void communication_task(
    void *arg)
{
    (void)arg;


    gateway_rx_message_t message;


    while (1)
    {
        if (xQueueReceive(
                rx_queue,
                &message,
                portMAX_DELAY) != pdTRUE)
        {
            continue;
        }


        if (memcmp(
                message.source_mac,
                camera_node_mac,
                ESP_NOW_ETH_ALEN) != 0)
        {
            continue;
        }


        if (message.data_len ==
            SGP_HMAC_PACKET_SIZE)
        {
            process_hello(
                &message
            );

            continue;
        }


        process_secure_packet(
            &message
        );
    }
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
        esp_now_register_recv_cb(
            espnow_receive_callback
        )
    );


    ESP_ERROR_CHECK(
        esp_now_register_send_cb(
            espnow_send_callback
        )
    );


    esp_now_peer_info_t peer = {0};


    memcpy(
        peer.peer_addr,
        camera_node_mac,
        ESP_NOW_ETH_ALEN
    );


    peer.channel =
        SGP_ESPNOW_CHANNEL;

    peer.ifidx =
        WIFI_IF_STA;

    peer.encrypt =
        false;


    ESP_ERROR_CHECK(
        esp_now_add_peer(
            &peer
        )
    );
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
    printf(" Role: GATEWAY\n");
    printf(" Firmware: 0.13.0\n");
    printf("==============================\n");


    ESP_ERROR_CHECK(
        sgp_security_init()
    );

    ESP_ERROR_CHECK(
    sgp_serial_export_init()
    );

    rx_queue =
        xQueueCreate(
            RX_QUEUE_LENGTH,
            sizeof(gateway_rx_message_t)
        );


    if (rx_queue == NULL)
    {
        ESP_LOGE(
            TAG,
            "RX queue creation failed"
        );

        return;
    }


    if (xTaskCreate(
            communication_task,
            "communication_task",
            7168,
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


    ESP_ERROR_CHECK(
        esp_read_mac(
            mac,
            ESP_MAC_WIFI_STA
        )
    );


    wifi_init();

    espnow_init();


    ESP_LOGI(
        TAG,
        "Gateway ready MAC=%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]
    );


    ESP_LOGI(
        TAG,
        "Waiting for encrypted JPEG images..."
    );
}