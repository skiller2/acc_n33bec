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


tone_t darth_vader[] = {
    {440, 500, 100},
    {440, 500, 100},
    {440, 500, 100},

    {349, 350, 50},
    {523, 150, 100},

    {440, 500, 100},
    {349, 350, 50},
    {523, 150, 100},

    {440, 1000, 200},

    {659, 500, 100},
    {659, 500, 100},
    {659, 500, 100},

    {698, 350, 50},
    {523, 150, 100},

    {415, 500, 100},
    {349, 350, 50},
    {523, 150, 100},

    {440, 1000, 200},
};


#define DOOR1_GPIO GPIO_NUM_21
#define DOOR2_GPIO GPIO_NUM_17
#define REX1_GPIO GPIO_NUM_16
#define REX2_GPIO GPIO_NUM_18
#define RELE3_GPIO GPIO_NUM_33
#define READER1_LED GPIO_NUM_45
#define READER2_LED GPIO_NUM_39
#define READER1_BUZZER GPIO_NUM_46
#define READER2_BUZZER GPIO_NUM_40

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
            if (e.reader == 1)
                beep(READER1_BUZZER, 500);
            else
                beep(READER2_BUZZER, 500);

            if (e.reader == 1)
                pulse_output(READER1_LED, 1000);
                //led(45, 1000);
            else
                pulse_output(READER2_LED, 1000);
                //led(39, 1000);

            ESP_LOGI(TAG, "worker: processing card=%llu from reader %d", e.card, e.reader);
            int ok = card_exists(e.card);
            int64_t ts = esp_timer_get_time();
            log_add(e.card, ts, e.reader, ok);
            ws_broadcast(e.card, ts, ok);
        }
    }
}

static void input_task(void *arg)
{
    static const char *TAG = "inputs";

    gpio_config_t io = {
        .pin_bit_mask =
            (1ULL << DOOR1_GPIO) |
            (1ULL << DOOR2_GPIO) |
            (1ULL << REX1_GPIO) |
            (1ULL << REX2_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // typical for switches
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};

    gpio_config(&io);

    int last_door1 = -1;
    int last_door2 = -1;
    int last_rex1 = -1;
    int last_rex2 = -1;

    while (1)
    {
        int door1 = gpio_get_level(DOOR1_GPIO);
        int door2 = gpio_get_level(DOOR2_GPIO);
        int rex1 = gpio_get_level(REX1_GPIO);
        int rex2 = gpio_get_level(REX2_GPIO);

        if (door1 != last_door1)
        {
            ESP_LOGI(TAG, "Door1: %s", door1 ? "OPEN" : "CLOSED");

            last_door1 = door1;
        }

        if (door2 != last_door2)
        {
            ESP_LOGI(TAG, "Door2: %s", door2 ? "OPEN" : "CLOSED");
            last_door2 = door2;
        }

        if (rex1 != last_rex1)
        {
            ESP_LOGI(TAG, "REX1: %s", !rex1 ? "ACTIVE" : "OFF");
            pulse_output(RELE3_GPIO,2000);
            last_rex1 = rex1;
        }

        if (rex2 != last_rex2)
        {
            ESP_LOGI(TAG, "REX2: %s", !rex2 ? "ACTIVE" : "OFF");
            pulse_output(RELE3_GPIO,2000);
            last_rex2 = rex2;
        }

        vTaskDelay(pdMS_TO_TICKS(500)); // debounce + CPU friendly
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
    log_add(0, 0, 0, 0);
    ESP_LOGI(TAG, "app_main complete");

    tone_t mario[] = {
        {660, 100, 50},
        {660, 100, 150},
        {660, 100, 150},
        {510, 100, 50},
        {660, 100, 150},
        {770, 100, 300},
        {380, 100, 300},

        {510, 100, 200},
        {380, 100, 200},
        {320, 100, 200},

        {440, 100, 150},
        {480, 80, 100},
        {450, 100, 150},
        {430, 100, 150},
        {380, 100, 200},

        {660, 80, 100},
        {760, 50, 100},
        {860, 100, 150},
        {700, 80, 100},
        {760, 50, 100},
        {660, 80, 100},

        {520, 80, 100},
        {580, 80, 100},
        {480, 80, 200},
    };

    //play_melody(READER1_BUZZER, mario, sizeof(mario) / sizeof(tone_t),1.2);
    play_melody_async(READER2_BUZZER, darth_vader, sizeof(darth_vader) / sizeof(tone_t),1.3);


    xTaskCreate(input_task, "input_task", 2048, NULL, 5, NULL);
}
