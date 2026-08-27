#include "barrier.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "config.h"
#include "beep.h"

static const char *TAG = "barrier";

void dispatch_log_event(uint8_t event_id, int port_id, uint64_t value, int64_t ts);

typedef enum {
    BARRIER_IDLE = 0,
    BARRIER_OPENING,
    BARRIER_OPEN,
    BARRIER_CLOSING
} barrier_state_t;

static barrier_state_t s_state = BARRIER_IDLE;
static TimerHandle_t s_timer = NULL;

#define BARRIER_OPENING_MS 3000
#define BARRIER_OPEN_MS    5000
#define BARRIER_CLOSING_MS 3000

static void barrier_set_relays(bool up, bool down)
{
    gpio_set_level(RELE1_GPIO, up ? 1 : 0);
    gpio_set_level(RELE2_GPIO, down ? 1 : 0);
}

static void barrier_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (s_state == BARRIER_OPENING) {
        s_state = BARRIER_OPEN;
        barrier_set_relays(false, false);
        xTimerChangePeriod(s_timer, pdMS_TO_TICKS(BARRIER_OPEN_MS), 0);
        xTimerStart(s_timer, 0);
        ESP_LOGI(TAG, "Barrier opened");
    } else if (s_state == BARRIER_OPEN) {
        s_state = BARRIER_CLOSING;
        barrier_set_relays(false, true);
        xTimerChangePeriod(s_timer, pdMS_TO_TICKS(BARRIER_CLOSING_MS), 0);
        xTimerStart(s_timer, 0);
        ESP_LOGI(TAG, "Barrier closing");
    } else if (s_state == BARRIER_CLOSING) {
        s_state = BARRIER_IDLE;
        barrier_set_relays(false, false);
        ESP_LOGI(TAG, "Barrier closed");
    }
}

void barrier_init(void)
{
    s_timer = xTimerCreate(
        "barrier_timer",
        pdMS_TO_TICKS(BARRIER_OPENING_MS),
        pdFALSE,
        NULL,
        barrier_timer_cb
    );

    if (!s_timer) {
        ESP_LOGE(TAG, "Failed to create barrier timer");
        return;
    }

    barrier_set_relays(false, false);
    ESP_LOGI(TAG, "Barrier control initialized");
}

void barrier_trigger_open(void)
{
    switch (s_state) {
        case BARRIER_IDLE:
            s_state = BARRIER_OPENING;
            barrier_set_relays(true, false);
            xTimerChangePeriod(s_timer, pdMS_TO_TICKS(BARRIER_OPENING_MS), 0);
            xTimerStart(s_timer, 0);
            ESP_LOGI(TAG, "Barrier opening from IDLE");
            break;

        case BARRIER_OPENING:
            xTimerReset(s_timer, 0);
            ESP_LOGI(TAG, "Barrier opening timer reset");
            break;

        case BARRIER_OPEN:
            xTimerReset(s_timer, 0);
            ESP_LOGI(TAG, "Barrier open timer reset");
            break;

        case BARRIER_CLOSING:
            xTimerStop(s_timer, 0);
            barrier_set_relays(false, false);
            s_state = BARRIER_OPENING;
            barrier_set_relays(true, false);
            xTimerChangePeriod(s_timer, pdMS_TO_TICKS(BARRIER_OPENING_MS), 0);
            xTimerStart(s_timer, 0);
            ESP_LOGI(TAG, "Barrier closing cancelled, opening");
            break;
    }
}

void barrier_position_reached_up(void)
{
    if (s_state == BARRIER_OPENING) {
        s_state = BARRIER_OPEN;
        barrier_set_relays(false, false);
        xTimerChangePeriod(s_timer, pdMS_TO_TICKS(BARRIER_OPEN_MS), 0);
        xTimerStart(s_timer, 0);
        ESP_LOGI(TAG, "Barrier fully opened by finish up switch");
    }
}

void barrier_position_reached_down(void)
{
    if (s_state == BARRIER_CLOSING) {
        s_state = BARRIER_IDLE;
        barrier_set_relays(false, false);
        xTimerStop(s_timer, 0);
        ESP_LOGI(TAG, "Barrier fully closed by finish down switch");
    }
}

void barrier_task(void *arg)
{
    (void)arg;
    barrier_init();

    gpio_config_t io = {
        .pin_bit_mask =
            (1ULL << REX1_GPIO) |
            (1ULL << REX2_GPIO) |
            (1ULL << LOOP_GPIO) |
            (1ULL << FINISH_UP_GPIO) |
            (1ULL << FINISH_DOWN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
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



    int last_rex1 = -1;
    int last_rex2 = -1;
    int last_loop = -1;
    int last_finish_up = -1;
    int last_finish_down = -1;

    while (1) {
        int rex1 = gpio_get_level(REX1_GPIO);
        int rex2 = gpio_get_level(REX2_GPIO);
        int loop = gpio_get_level(LOOP_GPIO);
        int finish_up = gpio_get_level(FINISH_UP_GPIO);
        int finish_down = gpio_get_level(FINISH_DOWN_GPIO);

        if (rex1 != last_rex1) {
            if (!rex1) {
                pulse_output_by_relay(g_config.rex1_relay_number, g_config.rex1_relay_duration_ms);
                barrier_trigger_open();
                ESP_LOGI(TAG, "REX1 activated relay %d", g_config.rex1_relay_number);
            }
            dispatch_log_event(6, 1, rex1, 0);
            last_rex1 = rex1;
        }

        if (rex2 != last_rex2) {
            if (!rex2) {
                pulse_output_by_relay(g_config.rex2_relay_number, g_config.rex2_relay_duration_ms);
                ESP_LOGI(TAG, "REX2 activated relay %d", g_config.rex2_relay_number);
            }
            dispatch_log_event(6, 2, rex2, 0);
            last_rex2 = rex2;
        }

        if (loop != last_loop) {
            if (!loop) {
                barrier_trigger_open();
                ESP_LOGI(TAG, "Loop detector triggered");
            }
            last_loop = loop;
        }

        if (finish_up != last_finish_up) {
            if (!finish_up) {
                barrier_position_reached_up();
                ESP_LOGI(TAG, "Finish up triggered");
            }
            last_finish_up = finish_up;
        }

        if (finish_down != last_finish_down) {
            if (!finish_down) {
                barrier_position_reached_down();
                ESP_LOGI(TAG, "Finish down triggered");
            }
            last_finish_down = finish_down;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
