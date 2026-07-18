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
#include "rtc.h"
#include "wiegand_local.h"
#include "esp_timer.h"
#include "beep.h"
#include "config.h"
#include "nvs_flash.h"
#include "example_common_private.h" 

static const char *TAG = "main";

    tone_t melody_ok[] = {
        {1200, 120, 30},
        {1600, 120, 30},
        {2000, 180, 60},
        {0,    50,  0},   // pause
        {1800, 250, 0}
    };

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

tone_t access_denied[] = {
    {700, 120, 20},
    {500, 120, 20},
    {300, 250, 0},
};



#define DOOR1_GPIO GPIO_NUM_21
#define DOOR2_GPIO GPIO_NUM_17

#define BAT_GPIO GPIO_NUM_34 //VERDE (Salida 12v)
#define CAR_GPIO GPIO_NUM_15 //AMARILLO (Pulso al morir)
#define ALI_GPIO GPIO_NUM_38 //ROJO Encendido tiene 220


#define REX1_GPIO GPIO_NUM_16
#define REX2_GPIO GPIO_NUM_18
#define READER1_BUZZER GPIO_NUM_46
#define READER2_BUZZER GPIO_NUM_40

extern void fs_init();
extern void http_init(QueueHandle_t qh);
extern void ws_init();
extern void card_store_init();
extern void log_store_init();
extern int card_exists(uint64_t);
extern void ws_broadcast(uint64_t, int64_t, int);
extern esp_err_t  send_json(uint8_t reader_id, uint64_t card_id);

static QueueHandle_t queue_cards;


void worker(void *p)
{
    ESP_LOGI(TAG, "worker started");
    evt_t e; // Card and reader event structure 
    gpio_num_t reader_relay_gpio;
    gpio_num_t reader_buzzer_gpio;
    uint32_t reader_relay_duration_ms;
    
    while (1)
    {
        if (xQueueReceive(queue_cards, &e, portMAX_DELAY))
        {
            if (e.reader == 1) { // Check which reader triggered the event and activate the corresponding relay and buzzer
                //pulse_output(g_config.reader1_relay_gpio, g_config.reader1_relay_duration_ms);
                //play_melody_async(READER1_BUZZER, mario, sizeof(mario) / sizeof(tone_t),1.3);
                reader_relay_gpio = g_config.reader1_relay_gpio;
                reader_buzzer_gpio = READER1_BUZZER;
                reader_relay_duration_ms = g_config.reader1_relay_duration_ms;
            } else {
                reader_relay_gpio = g_config.reader2_relay_gpio;
                reader_buzzer_gpio = READER2_BUZZER;
                reader_relay_duration_ms = g_config.reader2_relay_duration_ms;
                //pulse_output(g_config.reader2_relay_gpio, g_config.reader2_relay_duration_ms);
                //play_melody_async(READER2_BUZZER, mario, sizeof(mario) / sizeof(tone_t),1.3);
            }

            

            //ESP_LOGI(TAG, "worker: processing card=%llu from reader %d", e.card, e.reader);
            //ESP_LOGW(TAG, "Evaluando tarjeta %llu", e.card);
            int ok = card_exists(e.card) ? 1 : 0;
            if (ok)
            {
                //ESP_LOGI(TAG,"worker: card=%llu exists, access granted", e.card);
                pulse_output(reader_relay_gpio, reader_relay_duration_ms);
                play_melody_async(reader_buzzer_gpio, mario, sizeof(mario) / sizeof(tone_t), 1.3);
            }
            else
            {
                //ESP_LOGE(TAG,"worker: card=%llu does not exist, access denied", e.card);
                play_melody_async(reader_buzzer_gpio, access_denied, sizeof(access_denied) / sizeof(tone_t), 1.3);
                
            }
            time_t now;
            time(&now);
            /*
            struct tm tm_info;
            gmtime_r(&now, &tm_info);

            ESP_LOGI(TAG,
                    "Card %llu detected at %04d-%02d-%02d %02d:%02d:%02d UTC (reader %d)",
                    e.card,
                    tm_info.tm_year + 1900,
                    tm_info.tm_mon + 1,
                    tm_info.tm_mday,
                    tm_info.tm_hour,
                    tm_info.tm_min,
                    tm_info.tm_sec,
                    e.reader);*/
            send_json(e.reader, e.card); // Example call to send JSON data (replace with actual device and card IDs)
            //int ok = card_exists(e.card);
            //int64_t ts = esp_timer_get_time(); // Get the current timestamp in microseconds since boot
            log_add(e.card, now, e.reader, ok); // Log the card event with timestamp, reader ID, and access result
            ws_broadcast(e.card, now, ok);
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
            (1ULL << REX2_GPIO)|
            (1ULL << BAT_GPIO)|
            (1ULL << CAR_GPIO)|
            (1ULL << ALI_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // typical for switches
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};

    gpio_config(&io);

    int last_door1 = -1;
    int last_door2 = -1;
    int last_rex1 = -1;
    int last_rex2 = -1;
    int last_bat=-1;
    int last_car=-1;
    int last_ali=-1;

    while (1)
    {
        int door1 = gpio_get_level(DOOR1_GPIO);
        int door2 = gpio_get_level(DOOR2_GPIO);
        int rex1 = gpio_get_level(REX1_GPIO);
        int rex2 = gpio_get_level(REX2_GPIO);
        int bat = gpio_get_level(BAT_GPIO);
        int car = gpio_get_level(CAR_GPIO);
        int ali = gpio_get_level(ALI_GPIO);

        time_t now;
        time(&now);

        vTaskDelay(pdMS_TO_TICKS(500));

        
        // DOOR 1 TRACKING (Virtual IDs: 999101 / 999102)
        if (door1 != last_door1)
        {
            uint64_t virtual_card = door1 ? 999101 : 999102; // 101 = OPEN, 102 = CLOSED
            ESP_LOGI(TAG, "Door1: %s", door1 ? "OPEN" : "CLOSED");
            
            log_add(virtual_card, now, 1, 1);   
            send_json(1, virtual_card);         
            
            last_door1 = door1;
        }

        
        // DOOR 2 TRACKING (Virtual IDs: 999201 / 999202)
        if (door2 != last_door2)
        {
            uint64_t virtual_card = door2 ? 999201 : 999202; // 201 = OPEN, 202 = CLOSED
            ESP_LOGI(TAG, "Door2: %s", door2 ? "OPEN" : "CLOSED");
            
            log_add(virtual_card, now, 2, 1);   
            send_json(2, virtual_card);         
            
            last_door2 = door2;
        }

        
        // AC MAIN POWER TRACKING (Virtual IDs: 999301 / 999302)
        if (ali != last_ali)
        {
            uint64_t virtual_card = ali ? 999301 : 999302; // 301 = AC FAIL, 302 = AC OK
            ESP_LOGI(TAG, "Alimentacion: %s (%d)", ali ? "FALLA" : "OK", ali);
            
            //log_add(virtual_card, now, 0, ali ? 0 : 1); // Status = 0 if power is lost
            //send_json(0, virtual_card);                 // Dispatch JSON payload to server .235
            
            last_ali = ali;
        }

        if (bat != last_bat)
        {
            ESP_LOGI(TAG, "Bateria: %s (%d)", bat ? "FALLA" : "OK",bat);
            last_bat = bat;
        }

        if (car != last_car)
        {
            ESP_LOGI(TAG, "Carga: %s (%d)", car ? "OK" : "FALLA",car);
            last_car = car;
        }

        
        // REQUEST TO EXIT 1 (REX1) & RELAY 1 (Virtual ID: 999100)
        if (rex1 != last_rex1)
        {
            if (!rex1) {  // Active low edge trigger
                pulse_output(g_config.rex1_relay_gpio, g_config.rex1_relay_duration_ms);
                ESP_LOGI(TAG, "REX1 activated relay %d", g_config.rex1_relay_gpio);
                
                log_add(999100, now, 1, 1);     // Local log entry
                send_json(1, 999100);           // Upload to server .235
            }
            last_rex1 = rex1;
        }

        // REQUEST TO EXIT 2 (REX2) & RELAY 2 (Virtual ID: 999200)
        if (rex2 != last_rex2)
        {
            if (!rex2) {  // only trigger on transition to ACTIVE (0)
                pulse_output(g_config.rex2_relay_gpio, g_config.rex2_relay_duration_ms);
                ESP_LOGI(TAG, "REX2 activated relay %d", g_config.rex2_relay_gpio);
                
                log_add(999200, now, 2, 1);     
                send_json(2, 999200);         
            }
            last_rex2 = rex2;
        }

        vTaskDelay(pdMS_TO_TICKS(g_config.input_debounce_ms)); 
    }
}


/*
static void input_task(void *arg)
{
    static const char *TAG = "inputs";

    gpio_config_t io = {
        .pin_bit_mask =
            (1ULL << DOOR1_GPIO) |
            (1ULL << DOOR2_GPIO) |
            (1ULL << REX1_GPIO) |
            (1ULL << REX2_GPIO)|
            (1ULL << BAT_GPIO)|
            (1ULL << CAR_GPIO)|
            (1ULL << ALI_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // typical for switches
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};

    gpio_config(&io);

    int last_door1 = -1;
    int last_door2 = -1;
    int last_rex1 = -1;
    int last_rex2 = -1;
    int last_bat=-1;
    int last_car=-1;
    int last_ali=-1;

    while (1)
    {
        int door1 = gpio_get_level(DOOR1_GPIO);
        int door2 = gpio_get_level(DOOR2_GPIO);
        int rex1 = gpio_get_level(REX1_GPIO);
        int rex2 = gpio_get_level(REX2_GPIO);
        int bat = gpio_get_level(BAT_GPIO);
        int car = gpio_get_level(CAR_GPIO);
        int ali = gpio_get_level(ALI_GPIO);

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

        if (ali != last_ali)
        {
            ESP_LOGI(TAG, "Alimentacion: %s (%d)", ali ? "FALLA" : "OK", ali);
            last_ali = ali;
        }

        if (bat != last_bat)
        {
            ESP_LOGI(TAG, "Bateria: %s (%d)", bat ? "FALLA" : "OK",bat);
            last_bat = bat;
        }

        if (car != last_car)
        {
            ESP_LOGI(TAG, "Carga: %s (%d)", car ? "OK" : "FALLA",car);
            last_car = car;
        }

        if (rex1 != last_rex1)
        {
            ESP_LOGI(TAG, "REX1: %s", !rex1 ? "ACTIVE" : "OFF");
            if (!rex1) {  // only trigger on transition to ACTIVE (0)
                pulse_output(g_config.rex1_relay_gpio, g_config.rex1_relay_duration_ms);
                ESP_LOGI(TAG, "REX1 activated relay %d for %u ms", g_config.rex1_relay_gpio, g_config.rex1_relay_duration_ms);
            }
            last_rex1 = rex1;
        }

        if (rex2 != last_rex2)
        {
            ESP_LOGI(TAG, "REX2: %s", !rex2 ? "ACTIVE" : "OFF");
            if (!rex2) {  // only trigger on transition to ACTIVE (0)
                pulse_output(g_config.rex2_relay_gpio, g_config.rex2_relay_duration_ms);
                ESP_LOGI(TAG, "REX2 activated relay %d for %u ms", g_config.rex2_relay_gpio, g_config.rex2_relay_duration_ms);
            }
            last_rex2 = rex2;
        }
        vTaskDelay(pdMS_TO_TICKS(g_config.input_debounce_ms)); // debounce + CPU friendly
    }
}
*/
void wait_for_valid_time(void)
{
    while (1)
    {
        if (rtc_app_init() != ESP_OK)
        {
            ESP_LOGE(TAG, "RTC missing");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        time_t rtc_now;
        if (rtc_read_time(&rtc_now) == ESP_OK)
        {
            struct tm *tm_info = gmtime(&rtc_now);
            
            if (tm_info != NULL && (tm_info->tm_year + 1900) >= 2026)
            {
                if (rtc_set_system_time() == ESP_OK)
                {
                    ESP_LOGI(TAG, "Time restored successfully from RTC");
                    return;
                }
            }
            else
            {
                ESP_LOGE(TAG, "Invalid date");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Failed to read DS3231 registers");
        }

        if (fetch_and_store_time_in_nvs(NULL) == ESP_OK)
        {
            if (rtc_set_rtc_time() == ESP_OK)
            {
                ESP_LOGI(TAG, "RTC initialized from SNTP");
                return;
            }
        }

        ESP_LOGW(TAG, "Waiting for SNTP...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}


void time_sync_task(void *arg)
{

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(RTC_TIMESTAMP_UPDATE));
        ESP_LOGI(TAG, "Synchronizing time with SNTP...");
        if (fetch_and_store_time_in_nvs(NULL) == ESP_OK)
        {
            if (rtc_set_rtc_time() == ESP_OK)
            {
                ESP_LOGI(TAG, "RTC updated from SNTP");
            }
            else
            {
                ESP_LOGE(TAG, "Failed to update RTC from SNTP");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Failed to fetch time from SNTP");
        }

        
    }
}

void app_main()
{
    
    // =====================================
    // Queue

    ESP_LOGI(TAG, "Creating card event queue");
    queue_cards = xQueueCreate(64, sizeof(evt_t));
    if (!queue_cards)
    {
        ESP_LOGE(TAG, "Failed to create card event queue");
        return;
    }


    // =====================================

    ESP_LOGI(TAG, "app_main start");

    // ====================================
    // Initialize NVS
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(nvs_flash_init());  
    fs_init();
    // ====================================

    
    // ====================================
    //RTC DS3231 configuration and initialization
    
    // ====================================


    
    

    //=========================================
    // Ethernet initialization

    //ESP_LOGI(TAG, "Initializing Ethernet");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(ethernet_init());


    wait_for_valid_time();  
    //=========================================

    
    
    //initialize_sntp();
    //rtc_sync_time_from_sntp();
    //rtc_set_system_time();

    //=========================================
    // REX and log configuration and initialization
    
    ESP_LOGI(TAG, "Loading REX configuration");
    if (config_load(&g_config) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load REX config, using defaults");
    }


    ESP_LOGI(TAG, "Initializing card store");
    card_store_init();

    ESP_LOGI(TAG, "Initializing log store");
    log_store_init();

    //=========================================


    //=========================================
    // Readers initialization
    // Init Reader 1  //BEEP 46  //LED 45
    wiegand_init(48,47, 1, READER1_BUZZER, queue_cards);

    // Init Reader 2  //BEEP 40  //LED 39

    wiegand_init(41,42 , 2, READER2_BUZZER, queue_cards);


    //=========================================


    //ESP_ERROR_CHECK(example_ethernet_connect());
    //ESP_ERROR_CHECK(esp_register_shutdown_handler(&example_ethernet_shutdown));


    
    //fetch_and_store_time_in_nvs(NULL);
    //rtc_set_rtc_time();
    //rtc_set_system_time();


    //=========================================
    // HTTP and WebSocket server initialization
    http_init(queue_cards);

    ESP_LOGI(TAG, "Initializing WebSocket server");
    ws_init();

    //=========================================


    log_add(0, 0, 0, 0);
    ESP_LOGI(TAG, "app_main complete");


    //play_melody(READER1_BUZZER, mario, sizeof(mario) / sizeof(tone_t),1.2);
    //play_melody_async(READER2_BUZZER, darth_vader, sizeof(darth_vader) / sizeof(tone_t),1.3);



    //=========================================
    // Creating Tasks

    ESP_LOGI(TAG,"Creating update time task");
    if (xTaskCreate(time_sync_task, "time_sync_task", 4096, NULL, 3, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create time_sync_task");
    }

    ESP_LOGI(TAG, "Creating worker task");
    if (xTaskCreate(worker, "worker", 4096, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create worker task");
    }

    ESP_LOGI(TAG, "Creating input task");
    if (xTaskCreate(input_task, "input_task", 4096, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create input task");
    }
}