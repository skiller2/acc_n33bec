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
#include "wiegand_local.h"
#include "esp_timer.h"
#include "beep.h"

static const char *TAG = "main";

extern void fs_init();
extern void http_init();
extern void ws_init();
extern void card_store_init();
extern void log_store_init();
extern int card_exists(uint64_t);
extern void ws_broadcast(uint64_t, int64_t, int);

static QueueHandle_t queue_cards;

void worker(void *p)
{
    ESP_LOGI(TAG, "worker started");
    evt_t e;
    while (1)
    {
        if (xQueueReceive(queue_cards, &e, portMAX_DELAY))
        {
            if (e.reader==1)
                beep(46, 500);
            else 
                beep(40, 500);

            if (e.reader==1)
                led(45, 1000);
            else 
                led(39, 1000);

            ESP_LOGI(TAG, "worker: processing card=%llu from reader %d", e.card, e.reader);
            int ok = card_exists(e.card);
            int64_t ts = esp_timer_get_time();
            log_add(e.card, ts, e.reader,ok);
            ws_broadcast(e.card, ts, ok);
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
    queue_cards = xQueueCreate(64, sizeof(evt_t));
    if (!queue_cards)
    {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    // Init Reader 1  //BEEP 46  //LED 45
    wiegand_init(47, 48, 1, queue_cards);


    // Init Reader 2  //BEEP 40  //LED 39
    wiegand_init(42, 41, 2, queue_cards);

    ESP_LOGI(TAG, "Creating worker task");
    if (xTaskCreate(worker, "worker", 4096, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create worker task");
    }
    initialize_sntp();
    log_add(123456789, 0, 0,1);
    ESP_LOGI(TAG, "app_main complete");
}
