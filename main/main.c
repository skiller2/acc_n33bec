#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "connect.h"
#include "log_store.h"
#include "time_sync.h"


static const char *TAG = "main";

extern void fs_init();
extern void http_init();
extern void ws_init();
extern void wiegand_init(int,int,void(*cb)(uint64_t));
extern void card_store_init();
extern void log_store_init();
extern void process_card(uint64_t);

static QueueHandle_t queue;

typedef struct { uint64_t card; } evt_t;

void wiegand_cb(uint64_t c)
{
    evt_t e = {.card = c};
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(queue, &e, &xHigherPriorityTaskWoken) != pdTRUE) {
        ESP_EARLY_LOGW(TAG, "wiegand_cb: queue full, card=%llu", c);
    } else {
        ESP_EARLY_LOGI(TAG, "wiegand_cb: queued card=%llu", c);
    }
}

void worker(void* p)
{
    ESP_LOGI(TAG, "worker started");
    evt_t e;
    while (1) {
        if (xQueueReceive(queue, &e, portMAX_DELAY)) {
            ESP_LOGI(TAG, "worker: processing card=%llu", e.card);
            process_card(e.card);
        }
    }
}

void app_main()
{
    ESP_LOGI(TAG, "app_main start");
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Initializing filesystem");
    fs_init();

    ESP_LOGI(TAG, "Initializing card store");
    card_store_init();

    ESP_LOGI(TAG, "Initializing log store");
    log_store_init();


    ESP_ERROR_CHECK(esp_netif_init());    
    ESP_ERROR_CHECK(example_ethernet_connect());

    
    ESP_ERROR_CHECK(esp_register_shutdown_handler(&example_ethernet_shutdown));



    
    http_init();

    ESP_LOGI(TAG, "Initializing WebSocket server");
    ws_init();

    ESP_LOGI(TAG, "Creating event queue");
    queue = xQueueCreate(64, sizeof(evt_t));
    if (!queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    wiegand_init(47, 48, wiegand_cb);

    ESP_LOGI(TAG, "Creating worker task");
    if (xTaskCreate(worker, "worker", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create worker task");
    }
    initialize_sntp();
    log_add(123456789, 0, 1);
    ESP_LOGI(TAG, "app_main complete");
}
