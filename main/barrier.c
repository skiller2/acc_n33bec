#include "barrier.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "config.h"
#include "beep.h"
#include "ws.h"
#include <stdlib.h>
#include "freertos/semphr.h"

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
static SemaphoreHandle_t s_mutex = NULL;

static uint32_t s_time_up_ms = 0;
static uint32_t s_time_down_ms = 0;
static int64_t s_move_start_time = 0;
static uint8_t s_position_percent = 0;
static uint8_t s_last_broadcast_position = 0;


#define BARRIER_SWITCH_DELAY_MS 50

static bool s_last_up = false;
static bool s_last_down = false;

static void barrier_set_relays(bool up, bool down)
{
    if (up && down) {
        ESP_LOGE(TAG, "SAFETY CRASH: both barrier relays active");
        abort();
    }

    if (s_last_up != up || s_last_down != down) {
        if (s_last_up || s_last_down) {
            gpio_set_level(RELE1_GPIO, 0);
            gpio_set_level(RELE2_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(BARRIER_SWITCH_DELAY_MS));
        }
        gpio_set_level(RELE1_GPIO, up ? 1 : 0);
        gpio_set_level(RELE2_GPIO, down ? 1 : 0);
        s_last_up = up;
        s_last_down = down;
    }
}

static void barrier_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (!s_mutex) {
        ESP_LOGE(TAG, "Timer callback: not initialized");
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Timer callback: mutex timeout");
        return;
    }

    if (s_state == BARRIER_OPENING) {
        s_state = BARRIER_OPEN;
        barrier_set_relays(false, false);
        xTimerChangePeriod(s_timer, pdMS_TO_TICKS(g_barrier_config.barrier_open_ms), 0);
        xTimerStart(s_timer, 0);
        ESP_LOGI(TAG, "Barrier opened");
    } else if (s_state == BARRIER_OPEN) {
        if (gpio_get_level(LOOP_GPIO)==0){
            s_state = BARRIER_CLOSING;
            barrier_set_relays(false, true);
            s_move_start_time = esp_timer_get_time();
            s_position_percent = 100;
            xTimerChangePeriod(s_timer, pdMS_TO_TICKS(g_barrier_config.barrier_closing_ms), 0);
            xTimerStart(s_timer, 0);
            ESP_LOGI(TAG, "Barrier closing");
        } else {
            xTimerStart(s_timer, 0);
            ESP_LOGI(TAG, "Unable to close");
        }
        
    } else if (s_state == BARRIER_CLOSING) {
        s_state = BARRIER_IDLE;
        barrier_set_relays(false, false);
        ESP_LOGI(TAG, "Barrier closed");
    }

    xSemaphoreGive(s_mutex);
}

void barrier_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create barrier mutex");
        return;
    }

    s_time_up_ms = g_barrier_config.barrier_opening_ms;
    s_time_down_ms = g_barrier_config.barrier_closing_ms;

    s_timer = xTimerCreate(
        "barrier_timer",
        pdMS_TO_TICKS(g_barrier_config.barrier_opening_ms),
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
    if (!s_mutex || !s_timer) {
        ESP_LOGE(TAG, "barrier_trigger_open: not initialized");
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "barrier_trigger_open: mutex timeout");
        return;
    }

    if (!s_timer) {
        ESP_LOGE(TAG, "barrier_trigger_open: timer not initialized");
        xSemaphoreGive(s_mutex);
        return;
    }

    switch (s_state) {
        case BARRIER_IDLE:
            s_state = BARRIER_OPENING;
            barrier_set_relays(true, false);
            s_move_start_time = esp_timer_get_time();
            s_position_percent = 0;
            xTimerChangePeriod(s_timer, pdMS_TO_TICKS(g_barrier_config.barrier_opening_ms), 0);
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
            s_move_start_time = esp_timer_get_time();
            s_position_percent = 0;
            xTimerChangePeriod(s_timer, pdMS_TO_TICKS(g_barrier_config.barrier_opening_ms), 0);
            xTimerStart(s_timer, 0);
            ESP_LOGI(TAG, "Barrier closing cancelled, opening");
            break;
    }

    xSemaphoreGive(s_mutex);
}

void barrier_position_reached_up(void)
{
    if (!s_mutex || !s_timer) {
        ESP_LOGE(TAG, "barrier_position_reached_up: not initialized");
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "barrier_position_reached_up: mutex timeout");
        return;
    }

    if (s_state == BARRIER_OPENING) {
        int64_t now = esp_timer_get_time();
        int64_t elapsed_ms = (now - s_move_start_time) / 1000;
        if (elapsed_ms > 0 && elapsed_ms < 60000) {
            s_time_up_ms = (uint32_t)elapsed_ms;
        }
        s_state = BARRIER_OPEN;
        s_position_percent = 100;
        barrier_set_relays(false, false);
        if (s_timer) {
            xTimerChangePeriod(s_timer, pdMS_TO_TICKS(g_barrier_config.barrier_open_ms), 0);
            xTimerStart(s_timer, 0);
        }
        ESP_LOGI(TAG, "Barrier fully opened by finish up switch, time_up_ms=%u", s_time_up_ms);
    }

    xSemaphoreGive(s_mutex);
}

void barrier_position_reached_down(void)
{
    if (!s_mutex || !s_timer) {
        ESP_LOGE(TAG, "barrier_position_reached_down: not initialized");
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "barrier_position_reached_down: mutex timeout");
        return;
    }

    if (s_state == BARRIER_CLOSING) {
        int64_t now = esp_timer_get_time();
        int64_t elapsed_ms = (now - s_move_start_time) / 1000;
        if (elapsed_ms > 0 && elapsed_ms < 60000) {
            s_time_down_ms = (uint32_t)elapsed_ms;
        }
        s_state = BARRIER_IDLE;
        s_position_percent = 0;
        barrier_set_relays(false, false);
        if (s_timer) {
            xTimerStop(s_timer, 0);
        }
        ESP_LOGI(TAG, "Barrier fully closed by finish down switch, time_down_ms=%u", s_time_down_ms);
    }

    xSemaphoreGive(s_mutex);
}

static int barrier_calculate_position(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "barrier_calculate_position: mutex timeout");
        return 0;
    }

    int pos = 0;

    if (s_state == BARRIER_IDLE) {
        pos = 0;
    } else if (s_state == BARRIER_OPEN) {
        pos = 100;
    } else {
        int64_t now = esp_timer_get_time();
        int64_t elapsed_ms = (now - s_move_start_time) / 1000;

        if (s_state == BARRIER_OPENING) {
            if (s_time_up_ms == 0) {
                pos = 0;
            } else {
                pos = (int)((elapsed_ms * 100) / (int64_t)s_time_up_ms);
                if (pos > 100) pos = 100;
            }
        } else if (s_state == BARRIER_CLOSING) {
            if (s_time_down_ms == 0) {
                pos = 100;
            } else {
                pos = 100 - (int)((elapsed_ms * 100) / (int64_t)s_time_down_ms);
                if (pos < 0) pos = 0;
            }
        }
    }

    xSemaphoreGive(s_mutex);
    return pos;
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
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,  
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
    int last_rele1 = -1;
    int last_rele2 = -1;
    int last_rele3 = -1;

    while (1) {
        int rex1 = gpio_get_level(REX1_GPIO);
        int rex2 = gpio_get_level(REX2_GPIO);
        int loop = gpio_get_level(LOOP_GPIO);
        int finish_up = gpio_get_level(FINISH_UP_GPIO);
        int finish_down = gpio_get_level(FINISH_DOWN_GPIO);
        int rele1 = gpio_get_level(RELE1_GPIO);
        int rele2 = gpio_get_level(RELE2_GPIO);
        int rele3 = gpio_get_level(RELE3_GPIO);

        bool changed = false;

        if (rex1 != last_rex1) {
            if (!rex1) {
                barrier_trigger_open();
                ESP_LOGI(TAG, "REX1 activated relay %d", g_config.rex1_relay_number);
            }
            dispatch_log_event(6, 1, rex1, 0);
            last_rex1 = rex1;
            changed = true;
        }

        if (rex2 != last_rex2) {
            if (!rex2) {
                barrier_trigger_open();
                ESP_LOGI(TAG, "REX2 activated relay %d", g_config.rex2_relay_number);
            }
            dispatch_log_event(6, 2, rex2, 0);
            last_rex2 = rex2;
            changed = true;
        }

        if (loop != last_loop) {
            bool loop_triggered = g_barrier_config.loop_active_high ? (loop == 1) : (loop == 0);
            if (loop_triggered) {
                barrier_trigger_open();
                ESP_LOGI(TAG, "Loop detector triggered");
            }
            last_loop = loop;
            changed = true;
        }

        if (finish_up != last_finish_up) {
            if (!finish_up) {
                barrier_position_reached_up();
                ESP_LOGI(TAG, "Finish up triggered");
            }
            last_finish_up = finish_up;
            changed = true;
        }

        if (finish_down != last_finish_down) {
            if (!finish_down) {
                barrier_position_reached_down();
                ESP_LOGI(TAG, "Finish down triggered");
            }
            last_finish_down = finish_down;
            changed = true;
        }

        if (rele1 != last_rele1 || rele2 != last_rele2 || rele3 != last_rele3) {
            last_rele1 = rele1;
            last_rele2 = rele2;
            last_rele3 = rele3;
            changed = true;
        }

        s_position_percent = barrier_calculate_position();

        uint32_t time_up_ms = s_time_up_ms;
        uint32_t time_down_ms = s_time_down_ms;
        uint8_t position = s_position_percent;

        int pos_diff = position - s_last_broadcast_position;
        if (pos_diff < 0) pos_diff = -pos_diff;
        if (pos_diff >= 10) {
            s_last_broadcast_position = position;
            changed = true;
        }

        if (changed) {
            ws_broadcast_barrier(rex1, rex2, loop, finish_up, finish_down, rele1, rele2, rele3, time_up_ms, time_down_ms, g_barrier_config.barrier_open_ms, position, g_barrier_config.loop_active_high);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
