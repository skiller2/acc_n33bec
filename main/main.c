#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

extern void fs_init();
extern void wifi_init();
extern void http_init();
extern void wiegand_init(int, int, void (*cb)(uint64_t));
extern void card_store_init();
extern void log_store_init();

QueueHandle_t q;

typedef struct
{
    uint64_t card;
} evt_t;

void ISR_card(uint64_t c)
{
    evt_t e = {.card = c};
    xQueueSendFromISR(q, &e, NULL);
}

void card_task(void *p)
{
    evt_t e;
    while (1)
    {
        if (xQueueReceive(q, &e, portMAX_DELAY))
        {
            ESP_LOGI("CARD", "%llu", e.card);
        }
    }
}

void app_main()
{
    fs_init();
    card_store_init();
    log_store_init();
    wifi_init();
    http_init();
    q = xQueueCreate(32, sizeof(evt_t));
    wiegand_init(4, 5, ISR_card);
    xTaskCreate(card_task, "card", 4096, NULL, 5, NULL);
}
