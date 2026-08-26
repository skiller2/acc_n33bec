#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"
#include "connect.h"
#include "wifi.h"
#include "log_store.h"
#include "time_sync.h"
#include "rtc.h"
#include "wiegand_local.h"
#include "esp_timer.h"
#include "beep.h"
#include "config.h"
#include "ws.h"
#include "nvs_flash.h"
#include "example_common_private.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "driver/gpio.h"

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "dev"
#endif

static const char *TAG = "main";

EventGroupHandle_t s_ip_event_group;

tone_t melody_ok[] = {
    {1200, 120, 30},
    {1600, 120, 30},
    {2000, 180, 60},
    {0, 50, 0}, // pause
    {1800, 250, 0}};

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



extern void fs_init();
extern void http_init(QueueHandle_t qh);
extern void card_store_init();
extern void log_store_init();
extern int card_exists(uint64_t);
extern void ws_broadcast_card(uint64_t card, int64_t ts, int ok, char tipo_habilitacion, int64_t time_consuming, int port_id);
extern esp_err_t send_json(uint8_t event_id, uint8_t port_id, uint64_t value, uint32_t timeout);
extern esp_err_t send_json_card(uint8_t event_id, uint8_t port_id, uint64_t value, uint32_t timeout, bool *ok, char *tipo_habilitacion);
extern void ethernet_register_time_sync_task(TaskHandle_t task_handle);
void log_input_task(void *arg);
void dispatch_log_event(uint8_t event_id, int port_id, uint64_t value, int64_t ts);

typedef struct
{
    uint8_t event_id;
    int port_id;
    uint64_t value;
    int64_t ts;
    uint8_t send_retry;
} input_event_t;

static QueueHandle_t queue_cards;
static QueueHandle_t queue_inputs;

void log_input_task(void *arg)
{
    ESP_LOGI(TAG, "log input task started");

    input_event_t evt;

    ESP_LOGI(TAG, "waiting for network IP...");
    xEventGroupWaitBits(s_ip_event_group, HAVE_IP, pdTRUE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "got IP");

    while (1)
    {
        if (xQueueReceive(queue_inputs, &evt, portMAX_DELAY) == pdTRUE)
        {
            esp_err_t err = send_json(evt.event_id, evt.port_id, evt.value, 1200);

            if (err != ESP_OK)
            {
                evt.send_retry++;
                ESP_LOGW(TAG, "send_json failed (%s), requeueing event retry %d",
                         esp_err_to_name(err), evt.send_retry);

                if (xQueueSendToBack(queue_inputs, &evt, 0) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to requeue event");
                }

                vTaskDelay(pdMS_TO_TICKS(3000));
            }
        }
    }
}

void dispatch_log_event(uint8_t event_id, int port_id, uint64_t value, int64_t ts)
{
    log_add(event_id, port_id, value, ts);
    if (queue_inputs != NULL)
    {
        input_event_t evt = {
            .event_id = event_id,
            .port_id = port_id,
            .value = value,
            .ts = ts,
            .send_retry = 0};
        xQueueSendToBack(queue_inputs, &evt, 0);
    }
}

void worker(void *p)
{
    ESP_LOGI(TAG, "worker started");
    evt_t e; // Card and port event structure
    gpio_num_t port_buzzer_gpio;
    uint8_t event_id = 0;
    char tipo_habilitacion;
    bool ok = false;
    while (1)
    {
        if (xQueueReceive(queue_cards, &e, portMAX_DELAY))
        {
            int64_t t0 = esp_timer_get_time();

            if (e.port_id == 1)
            {
                port_buzzer_gpio = PORT1_BUZZER;
            }
            else
            {
                port_buzzer_gpio = PORT2_BUZZER;
            }

            uint64_t now;
            now = getTimeStamp(); // Get the current timestamp in microseconds since epoch
            tipo_habilitacion='X';
            esp_err_t res = send_json_card(9, e.port_id, e.card, 2000, &ok, &tipo_habilitacion);
            if (res == ESP_ERR_HTTP_FETCH_HEADER || res == ESP_ERR_HTTP_WRITE_DATA)
                res = send_json_card(9, e.port_id, e.card, 2000, &ok, &tipo_habilitacion);
            int64_t t1 = esp_timer_get_time();

            ESP_LOGW(TAG, "Respuesta de tarjeta: %d, tipo habilitacion %c,  en %llu us", ok, tipo_habilitacion, t1 - t0);

            if (res == ESP_OK)
            {
                // Hago el analisis de la tarjeta y determino si es valida o no, para enviar al log el evento correspondiente
            }
            else
            {
                ok = card_exists(e.card) ? 1 : 0;
            }

            if (ok)
            {
                ESP_LOGI(TAG, "worker: card=%llu exists, access granted", e.card);
                if (e.port_id == 1)
                {
                    pulse_output_by_relay(g_config.port1_relay_number, g_config.port1_relay_duration_ms);

                    if (tipo_habilitacion == 84)
                    {
                        pulse_output_by_relay(g_config.port1_relay2_number, g_config.port1_relay2_duration_ms);
                    }
                }
                else
                {
                    pulse_output_by_relay(g_config.port2_relay_number, g_config.port2_relay_duration_ms);
                    if (tipo_habilitacion == 84)
                    {
                        pulse_output_by_relay(g_config.port2_relay2_number, g_config.port2_relay2_duration_ms);
                    }
                }
                play_melody_async(port_buzzer_gpio, mario, sizeof(mario) / sizeof(tone_t), 1.3);
                event_id = 10; // CARD PASS
            }
            else
            {
                event_id = 11; // CARD REJECT

                // ESP_LOGE(TAG,"worker: card=%llu does not exist, access denied", e.card);
                // heap_caps_check_integrity_all(true);
                play_melody_async(port_buzzer_gpio, access_denied, sizeof(access_denied) / sizeof(tone_t), 1.3);
                // heap_caps_check_integrity_all(true);
            }
            log_add(event_id, e.port_id, e.card, now);

            // dispatch_log_event(event_id,e.port_id,e.card,now);

            ws_broadcast_card(e.card, now, ok, tipo_habilitacion, t1 - t0, e.port_id);
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
            (1ULL << REX2_GPIO) |
            (1ULL << BAT_GPIO) |
            (1ULL << CAR_GPIO) |
            (1ULL << ALI_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // typical for switches
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};

    gpio_config(&io);


    gpio_config_t io_out = {
        .pin_bit_mask = (1ULL << RELE3_GPIO ) | (1ULL << RELE2_GPIO ) | (1ULL << RELE1_GPIO ),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_out);

    gpio_set_level(RELE1_GPIO,0);
    gpio_set_level(RELE2_GPIO,0);
    gpio_set_level(RELE3_GPIO,0);

    int last_door1 = -1;
    int last_door2 = -1;
    int last_rex1 = -1;
    int last_rex2 = -1;
    int last_bat = -1;
    int last_car = -1;
    int last_ali = -1;
    int last_rele1 = -1;
    int last_rele2 = -1;
    int last_rele3 = -1;

    while (1)
    {
        int door1 = gpio_get_level(DOOR1_GPIO);
        int door2 = gpio_get_level(DOOR2_GPIO);
        int rex1 = gpio_get_level(REX1_GPIO);
        int rex2 = gpio_get_level(REX2_GPIO);
        int bat = gpio_get_level(BAT_GPIO);
        int car = gpio_get_level(CAR_GPIO);
        int ali = gpio_get_level(ALI_GPIO);
        int rele1 = gpio_get_level(RELE1_GPIO);
        int rele2 = gpio_get_level(RELE2_GPIO);
        int rele3 = gpio_get_level(RELE3_GPIO);

        if (door1 != last_door1 || door2 != last_door2 || rex1 != last_rex1 || rex2 != last_rex2 ||
            rele1 != last_rele1 || rele2 != last_rele2 || rele3 != last_rele3)
        {
            ws_broadcast_io_status(door1, door2, rex1, rex2, rele1, rele2, rele3);
        }

        /*
        if (++ws_io_seq >= 10)
        {
            ws_io_seq = 0;
            ws_broadcast_io_status(door1, door2, rex1, rex2, rele1, rele2, rele3);
        }
            */

        if (door1 != last_door1)
        {
            ESP_LOGI(TAG, "Door1: %s", door1 ? "OPEN" : "CLOSED");

            dispatch_log_event(5, 1, door1, 0);

            last_door1 = door1;
        }

        if (door2 != last_door2)
        {
            ESP_LOGI(TAG, "Door2: %s", door2 ? "OPEN" : "CLOSED");

            dispatch_log_event(5, 2, door2, 0);

            last_door2 = door2;
        }

        if (ali != last_ali)
        {
            ESP_LOGI(TAG, "Alimentacion: %s (%d)", ali ? "FALLA" : "OK", ali);

            dispatch_log_event(2, 0, ali, 0);

            last_ali = ali;
        }

        if (bat != last_bat)
        {
            ESP_LOGI(TAG, "Bateria: %s (%d)", bat ? "FALLA" : "OK", bat);
            last_bat = bat;
        }

        if (car != last_car)
        {
            ESP_LOGI(TAG, "Carga: %s (%d)", car ? "OK" : "FALLA", car);
            last_car = car;
        }

        // REQUEST TO EXIT 1 (REX1) & RELAY 1 (Virtual ID: 999100)
        if (rex1 != last_rex1)
        {
            if (!rex1)
            { // Active low edge trigger
                pulse_output_by_relay(g_config.rex1_relay_number, g_config.rex1_relay_duration_ms);
                ESP_LOGI(TAG, "REX1 activated relay %d", g_config.rex1_relay_number);
            }

            dispatch_log_event(6, 1, rex1, 0);
            last_rex1 = rex1;
        }

        // REQUEST TO EXIT 2 (REX2) & RELAY 2 (Virtual ID: 999200)
        if (rex2 != last_rex2)
        {
            if (!rex2)
            { // only trigger on transition to ACTIVE (0)
                pulse_output_by_relay(g_config.rex2_relay_number, g_config.rex2_relay_duration_ms);
                ESP_LOGI(TAG, "REX2 activated relay %d", g_config.rex2_relay_number);
            }
            dispatch_log_event(6, 2, rex2, 0);
            last_rex2 = rex2;
        }

        last_rele1 = rele1;
        last_rele2 = rele2;
        last_rele3 = rele3;

        vTaskDelay(pdMS_TO_TICKS(g_config.input_debounce_ms));
    }
}

void wait_for_valid_time(void)
{
    while (1)
    {
        if (isTimeNTP())
        {
            ESP_LOGI(TAG, "Time set via NTP");
            return;
        }

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
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void time_sync_task(void *arg)
{
    xEventGroupWaitBits(s_ip_event_group, HAVE_IP, pdTRUE, pdFALSE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(5 * 1000));
    while (1)
    {

        if (fetch_and_store_time_in_nvs(NULL) == ESP_OK)
        {
            rtc_set_rtc_time();
        }
        vTaskDelay(pdMS_TO_TICKS(60 * 60 * 1000));
    }
}
static void keep_alive_task(void *arg)
{
    const char *TAG = "keep_alive";
    ESP_LOGI(TAG, "keep alive task started");

    ESP_LOGI(TAG, "waiting for network IP...");
    xEventGroupWaitBits(s_ip_event_group, HAVE_IP, pdTRUE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "got IP");

    while (1)
    {
        // Read the current keep alive interval from global config

        // If interval is zero, we skip sending (but we set a default of 30, so it should be non-zero)
        if (g_config.keep_alive_secs > 0)
        {
            // Send a keep-alive JSON packet
            // We'll use event_id 20 for keep-alive, port_id 0 (not associated with a physical port), and value as the device_id
            esp_err_t err = send_json(20, 0, 1, 1000);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG, "keep-alive send failed: %s", esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG, "Sent keep-alive JSON, interval: %lu secs", g_config.keep_alive_secs);
            }
        }

        // Wait for the specified interval (convert seconds to milliseconds for vTaskDelay)
        vTaskDelay(pdMS_TO_TICKS(g_config.keep_alive_secs * 1000));
    }
}

void app_main()
{

    // =====================================
    // Queue

    ESP_LOGI(TAG, "Creating input event queue");
    queue_inputs = xQueueCreate(64, sizeof(input_event_t));
    if (!queue_inputs)
    {
        ESP_LOGE(TAG, "Failed to create input event queue");
    }

    ESP_LOGI(TAG, "Creating card event queue");
    queue_cards = xQueueCreate(64, sizeof(evt_t));
    if (!queue_cards)
    {
        ESP_LOGE(TAG, "Failed to create card event queue");
        return;
    }

    s_ip_event_group = xEventGroupCreate();

    // =====================================

    ESP_LOGI(TAG, "app_main start");

    // ====================================
    // Initialize NVS
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(nvs_flash_init());
    fs_init();
    // ====================================

    // ====================================
    // RTC DS3231 configuration and initialization

    // ====================================

    //=========================================
    // Ethernet initialization

    // ESP_LOGI(TAG, "Initializing Ethernet");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(ethernet_init());

    http_init(queue_cards);

    ESP_LOGI(TAG, "Loading REX configuration");
    if (config_load(&g_config) != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load REX config, using defaults");
    }

    ESP_LOGI(TAG, "Initializing card store");
    card_store_init();

    ESP_LOGI(TAG, "Initializing log store");
    log_store_init();

    ESP_LOGI(TAG, "Creating update time task");
    if (xTaskCreate(time_sync_task, "time_sync_task", 4096, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create time_sync_task");
    }

    if (rtc_app_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "RTC NOT WORKING");
    }

#if !CONFIG_SKIP_WAIT_FOR_RTC
    wait_for_valid_time();
#endif

    if (wifi_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi/DPP initialization failed");
    }

    //=========================================

    // initialize_sntp();
    // rtc_sync_time_from_sntp();
    // rtc_set_system_time();

    //=========================================
    // REX and log configuration and initialization

    //=========================================

    //=========================================
    // Ports initialization
    // Init PORT 1  //BEEP 46  //LED 45
    wiegand_init(48, 47, 1, PORT1_BUZZER, queue_cards);

    // Init PORT 2  //BEEP 40  //LED 39

    wiegand_init(41, 42, 2, PORT2_BUZZER, queue_cards);

    //=========================================

    // ESP_ERROR_CHECK(example_ethernet_connect());
    // ESP_ERROR_CHECK(esp_register_shutdown_handler(&example_ethernet_shutdown));

    // fetch_and_store_time_in_nvs(NULL);
    // rtc_set_rtc_time();
    // rtc_set_system_time();

    //=========================================
    // HTTP and WebSocket server initialization

    //=========================================

    log_add(1, 0, 0, 0);
    ESP_LOGI(TAG, "app_main complete - version %s", PROJECT_VERSION);

    // play_melody(PORT1_BUZZER, mario, sizeof(mario) / sizeof(tone_t),1.2);
    // play_melody_async(PORT2_BUZZER, darth_vader, sizeof(darth_vader) / sizeof(tone_t),1.3);

    //=========================================
    // Creating Tasks

    ESP_LOGI(TAG, "Creating log input task");
    if (xTaskCreate(log_input_task, "log input task", 8192, NULL, 4, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create log input task");
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

    ESP_LOGI(TAG, "Creating keep alive task");
    if (xTaskCreate(keep_alive_task, "keep_alive_task", 4096, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create keep_alive_task");
    }
}