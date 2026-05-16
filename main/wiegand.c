#include <stdio.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <wiegand.h>
#include <esp_log.h>
#include <string.h>

static const char *TAG = "wiegand_reader";

static wiegand_reader_t reader;
static QueueHandle_t queue = NULL;

#define CONFIG_EXAMPLE_BUF_SIZE 64

typedef struct
{
    uint8_t data[CONFIG_EXAMPLE_BUF_SIZE];
    size_t bits;
} data_packet_t;

typedef struct
{
    int d0;
    int d1;
    void (*cb)(uint64_t);
} wiegand_task_args_t;

// callback on new data in reader
static void reader_callback(wiegand_reader_t *r)
{
    // you can decode raw data from reader buffer here, but remember:
    // reader will ignore any new incoming data while executing callback

    // create simple undecoded data packet
    data_packet_t p;
    p.bits = r->bits;
    memcpy(p.data, r->buf, CONFIG_EXAMPLE_BUF_SIZE);

    // Send it to the queue
    xQueueSendToBack(queue, &p, 0);
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

static void task(void *arg)
{
    if (arg == NULL) {
        ESP_LOGE(TAG, "No task args provided");
        vTaskDelete(NULL);
        return;
    }

    wiegand_task_args_t *task_args = (wiegand_task_args_t *)arg;

    // Create queue
    queue = xQueueCreate(5, sizeof(data_packet_t));
    if (!queue)
    {
        ESP_LOGE(TAG, "Error creating queue");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    // Initialize reader with dynamic GPIO pins
    ESP_ERROR_CHECK(wiegand_reader_init(&reader, task_args->d0, task_args->d1,
                                        true, CONFIG_EXAMPLE_BUF_SIZE, reader_callback, WIEGAND_MSB_FIRST, WIEGAND_LSB_FIRST));

    data_packet_t p;
    while (1)
    {
        ESP_LOGI(TAG, "Waiting for Wiegand data...");
        if (xQueueReceive(queue, &p, portMAX_DELAY) == pdTRUE) {


// dump received data
        printf("==========================================\n");
        printf("Bits received: %d\n", p.bits);
        printf("Received data:");
        int bytes = p.bits / 8;
        int tail = p.bits % 8;
        for (size_t i = 0; i < bytes + (tail ? 1 : 0); i++)
            printf(" 0x%02x", p.data[i]);
        printf("\n==========================================\n");


            if (task_args->cb) {
                uint64_t card_value = decode_packet(&p);
                task_args->cb(card_value);
            }
        }
    }
}

void wiegand_init(int d0, int d1, void (*f)(uint64_t))
{
    ESP_LOGI(TAG, "Initializing Wiegand input on D0=%d, D1=%d", d0, d1);

    static wiegand_task_args_t task_args;
    task_args.d0 = d0;
    task_args.d1 = d1;
    task_args.cb = f;

    if (xTaskCreate(task, TAG, configMINIMAL_STACK_SIZE * 4, &task_args, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create Wiegand task");
        return;
    }
}
