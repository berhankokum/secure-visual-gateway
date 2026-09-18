#include "sgp_serial_export.h"

#include <stdio.h>
#include <stdbool.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "mbedtls/base64.h"


static const char *TAG =
    "SERIAL_EXPORT";


#define EXPORT_QUEUE_LENGTH     2U
#define BASE64_INPUT_CHUNK      57U
#define BASE64_OUTPUT_SIZE      128U


typedef struct
{
    uint32_t session_id;

    uint32_t image_id;

    uint16_t width;
    uint16_t height;

    uint8_t *data;

    size_t data_len;

} serial_export_item_t;


static QueueHandle_t export_queue =
    NULL;


static void serial_export_task(
    void *arg)
{
    (void)arg;


    serial_export_item_t item;


    while (1)
    {
        if (xQueueReceive(
                export_queue,
                &item,
                portMAX_DELAY) != pdTRUE)
        {
            continue;
        }


        bool success =
            true;


        printf(
            "@@SGP_IMG_BEGIN "
            "session=0x%08lX "
            "id=%lu "
            "size=%u "
            "width=%u "
            "height=%u\n",
            (unsigned long)item.session_id,
            (unsigned long)item.image_id,
            (unsigned)item.data_len,
            item.width,
            item.height
        );


        size_t offset =
            0U;


        while (offset <
               item.data_len)
        {
            const size_t remaining =
                item.data_len -
                offset;


            const size_t input_len =
                (remaining >
                 BASE64_INPUT_CHUNK)
                    ? BASE64_INPUT_CHUNK
                    : remaining;


            unsigned char encoded[
                BASE64_OUTPUT_SIZE
            ] = {0};


            size_t encoded_len =
                0U;


            const int result =
                mbedtls_base64_encode(
                    encoded,
                    sizeof(encoded) - 1U,
                    &encoded_len,
                    &item.data[offset],
                    input_len
                );


            if ((result != 0) ||
                (encoded_len >=
                 sizeof(encoded)))
            {
                success =
                    false;

                break;
            }


            encoded[encoded_len] =
                '\0';


            printf(
                "@@SGP_IMG_DATA %s\n",
                encoded
            );


            offset +=
                input_len;
        }


        if (success)
        {
            printf(
                "@@SGP_IMG_END "
                "session=0x%08lX "
                "id=%lu\n",
                (unsigned long)item.session_id,
                (unsigned long)item.image_id
            );


            ESP_LOGI(
                TAG,
                "Image exported session=0x%08lX id=%lu bytes=%u",
                (unsigned long)item.session_id,
                (unsigned long)item.image_id,
                (unsigned)item.data_len
            );
        }
        else
        {
            printf(
                "@@SGP_IMG_ABORT "
                "session=0x%08lX "
                "id=%lu\n",
                (unsigned long)item.session_id,
                (unsigned long)item.image_id
            );


            ESP_LOGE(
                TAG,
                "Base64 export failed"
            );
        }


        fflush(
            stdout
        );


        if (item.data != NULL)
        {
            heap_caps_free(
                item.data
            );
        }
    }
}


esp_err_t sgp_serial_export_init(void)
{
    if (export_queue != NULL)
    {
        return ESP_OK;
    }


    export_queue =
        xQueueCreate(
            EXPORT_QUEUE_LENGTH,
            sizeof(serial_export_item_t)
        );


    if (export_queue == NULL)
    {
        return ESP_ERR_NO_MEM;
    }


    if (xTaskCreate(
            serial_export_task,
            "serial_export_task",
            4096,
            NULL,
            4,
            NULL) != pdPASS)
    {
        vQueueDelete(
            export_queue
        );


        export_queue =
            NULL;


        return ESP_ERR_NO_MEM;
    }


    ESP_LOGI(
        TAG,
        "Serial image export ready"
    );


    return ESP_OK;
}


esp_err_t sgp_serial_export_submit(
    uint32_t session_id,
    uint32_t image_id,
    uint16_t width,
    uint16_t height,
    uint8_t *data,
    size_t data_len)
{
    if ((export_queue == NULL) ||
        (data == NULL) ||
        (data_len == 0U))
    {
        return ESP_ERR_INVALID_STATE;
    }


    const serial_export_item_t item =
    {
        .session_id =
            session_id,

        .image_id =
            image_id,

        .width =
            width,

        .height =
            height,

        .data =
            data,

        .data_len =
            data_len
    };


    if (xQueueSend(
            export_queue,
            &item,
            0) != pdTRUE)
    {
        return ESP_ERR_NO_MEM;
    }


    return ESP_OK;
}