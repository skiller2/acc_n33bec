#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <string.h>
#include <wiegand.h>
#include <wiegand_local.h>

static const char *TAG = "wiegand_reader";

#define CONFIG_EXAMPLE_BUF_SIZE 64
#define MAX_WIEGAND_READERS 2

typedef struct
{
    uint8_t data[CONFIG_EXAMPLE_BUF_SIZE];
    size_t bits;
} data_packet_t;

typedef struct
{
    int d0;
    int d1;
    int reader_id;
    QueueHandle_t packet_queue;
    QueueHandle_t event_queue;
    wiegand_reader_t reader;
} wiegand_task_args_t;

static wiegand_task_args_t *reader_args[MAX_WIEGAND_READERS];
static int reader_args_count = 0;

static bool register_reader_args(wiegand_task_args_t *args)
{
    if (reader_args_count >= MAX_WIEGAND_READERS) {
        return false;
    }
    reader_args[reader_args_count++] = args;
    return true;
}

static wiegand_task_args_t *find_reader_args(wiegand_reader_t *reader)
{
    for (int i = 0; i < reader_args_count; i++) {
        if (&reader_args[i]->reader == reader) {
            return reader_args[i];
        }
    }
    return NULL;
}

// callback on new data in reader
static void reader_callback(wiegand_reader_t *r)
{
    wiegand_task_args_t *task_args = find_reader_args(r);
    if (!task_args || task_args->packet_queue == NULL) {
        ESP_LOGW(TAG, "reader_callback: unknown reader or missing queue");
        return;
    }

    // create simple undecoded data packet
    data_packet_t p;
    p.bits = r->bits;
    memcpy(p.data, r->buf, CONFIG_EXAMPLE_BUF_SIZE);

    // Send it to the queue
    xQueueSendToBack(task_args->packet_queue, &p, 0);
}

static uint64_t decode_packet(const data_packet_t *p)
{
    uint64_t value = 0;
    int bytes = p->bits / 8;
    int tail = p->bits % 8;
    int total_bytes = bytes + (tail ? 1 : 0);

    for (int i = 0; i < total_bytes; i++) {
        value <<= 8;
        value |= p->data[i];
    }

    return value;
}

static void wiegand_tsk(void *arg)
{
    if (arg == NULL) {
        ESP_LOGE(TAG, "No task args provided");
        vTaskDelete(NULL);
        return;
    }

    wiegand_task_args_t *task_args = (wiegand_task_args_t *)arg;

    ESP_ERROR_CHECK(wiegand_reader_init(&task_args->reader, task_args->d0, task_args->d1,
                                        true, CONFIG_EXAMPLE_BUF_SIZE, reader_callback,
                                        WIEGAND_MSB_FIRST, WIEGAND_LSB_FIRST));

    data_packet_t p;
    while (1) {
        ESP_LOGI(TAG, "Waiting for Wiegand data (reader %d)...", task_args->reader_id);
        if (xQueueReceive(task_args->packet_queue, &p, portMAX_DELAY) == pdTRUE) {
            // dump received data
            printf("==========================================\n");
            printf("Bits received: %d\n", p.bits);
            printf("Received data:");
            int bytes = p.bits / 8;
            int tail = p.bits % 8;
            for (size_t i = 0; i < bytes + (tail ? 1 : 0); i++) {
                printf(" 0x%02x", p.data[i]);
            }
            printf("\n==========================================\n");

            if (task_args->event_queue) {
                uint64_t card_value = decode_packet(&p);
                evt_t e = {.card = card_value, .reader = task_args->reader_id};

                if (xQueueSendToBack(task_args->event_queue, &e, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "wiegand_tsk: event queue full, card=%llu", card_value);
                } else {
                    ESP_LOGI(TAG, "wiegand_tsk: queued card=%llu from reader %d", card_value, task_args->reader_id);
                }
            }
        }
    }
}

void wiegand_init(int d0, int d1, int reader, QueueHandle_t qh)
{
    ESP_LOGI(TAG, "Initializing Wiegand Reader %d input on D0=%d, D1=%d", reader, d0, d1);

    gpio_set_direction(d0, GPIO_MODE_INPUT);
    gpio_set_direction(d1, GPIO_MODE_INPUT);

    wiegand_task_args_t *task_args = calloc(1, sizeof(wiegand_task_args_t));
    if (!task_args) {
        ESP_LOGE(TAG, "Failed to allocate task args");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
        return;
    }

    task_args->d0 = d0;
    task_args->d1 = d1;
    task_args->reader_id = reader;
    task_args->event_queue = qh;
    task_args->packet_queue = xQueueCreate(5, sizeof(data_packet_t));
    if (!task_args->packet_queue) {
        ESP_LOGE(TAG, "Error creating packet queue");
        free(task_args);
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
        return;
    }

    if (!register_reader_args(task_args)) {
        ESP_LOGE(TAG, "Too many Wiegand readers configured");
        vQueueDelete(task_args->packet_queue);
        free(task_args);
        return;
    }

    if (xTaskCreate(wiegand_tsk, TAG, configMINIMAL_STACK_SIZE * 4, task_args, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Wiegand task");
        vQueueDelete(task_args->packet_queue);
        free(task_args);
        return;
    }
}
