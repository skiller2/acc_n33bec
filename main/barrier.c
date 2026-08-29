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
#include <stdio.h>
#include "freertos/semphr.h"

static const char *TAG = "barrier";

void dispatch_log_event(uint8_t event_id, int port_id, uint64_t value, int64_t ts);

typedef enum
{
    BARRIER_IDLE = 0,
    BARRIER_OPENING,
    BARRIER_OPEN,
    BARRIER_CLOSING,
    BARRIER_ERROR
} barrier_state_t;

static barrier_state_t s_state = BARRIER_IDLE;
static TimerHandle_t s_timer = NULL;
static SemaphoreHandle_t s_mutex = NULL;

static uint32_t s_time_up_ms = 0;
static uint32_t s_time_down_ms = 0;
static int64_t s_move_start_time = 0;

static int64_t s_open_expire_time = 0;

static bool s_error = false;
static bool s_move_timeout = false;
static char s_error_msg[80] = "";

#define BARRIER_SWITCH_DELAY_MS 50

static bool s_last_up = false;
static bool s_last_down = false;
static int64_t s_relay_switch_time = 0;
static bool s_relay_switch_pending = false;

static bool barrier_read_loop(void)
{
    int val = gpio_get_level(LOOP_GPIO);
    return g_barrier_config.loop_active_high ? (val == 1) : (val == 0);
}

static void barrier_set_relays(bool up, bool down)
{
    if (up && down)
    {
        ESP_LOGE(TAG, "SAFETY CRASH: both barrier relays active");
        abort();
    }

    int64_t now = esp_timer_get_time();
    int64_t elapsed_since_switch = (now - s_relay_switch_time) / 1000;

    if (s_last_up == up && s_last_down == down)
    {
        return;
    }

    if ((s_last_up || s_last_down) && elapsed_since_switch < BARRIER_SWITCH_DELAY_MS)
    {
        s_relay_switch_pending = true;
        return;
    }

    if (s_last_up || s_last_down)
    {
        gpio_set_level(RELE1_GPIO, 0);
        gpio_set_level(RELE2_GPIO, 0);
        s_relay_switch_time = now;
        s_relay_switch_pending = true;

        if (up)
        {
            s_last_up = false;
            s_last_down = false;
        }
        else if (down)
        {
            s_last_up = false;
            s_last_down = false;
        }
        return;
    }

    gpio_set_level(RELE1_GPIO, up ? 1 : 0);
    gpio_set_level(RELE2_GPIO, down ? 1 : 0);
    s_last_up = up;
    s_last_down = down;
    s_relay_switch_time = now;
    s_relay_switch_pending = false;
}

static void barrier_stop_relays(void)
{
    gpio_set_level(RELE1_GPIO, 0);
    gpio_set_level(RELE2_GPIO, 0);
    s_last_up = false;
    s_last_down = false;
    s_relay_switch_pending = false;
}

static void barrier_set_error(const char *msg)
{
    s_error = true;
    snprintf(s_error_msg, sizeof(s_error_msg), "%s", msg);
    barrier_stop_relays();
    if (s_timer)
    {
        xTimerStop(s_timer, 0);
    }
    s_state = BARRIER_ERROR;
    ESP_LOGE(TAG, "Barrier error: %s", msg);
}

static void barrier_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (!s_mutex)
    {
        ESP_LOGE(TAG, "Timer callback: not initialized");
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Timer callback: mutex timeout");
        return;
    }

    s_error = false;
    s_error_msg[0] = '\0';

    if (s_state == BARRIER_OPENING)
    {
        s_state = BARRIER_OPEN;
        s_open_expire_time = esp_timer_get_time() + ((int64_t)g_barrier_config.barrier_open_ms * 1000);
        //s_position_percent = 100;
        barrier_stop_relays();

        xTimerChangePeriod(s_timer, pdMS_TO_TICKS(g_barrier_config.barrier_open_ms), 0);
        xTimerStart(s_timer, 0);
        s_move_timeout = true;
        ESP_LOGI(TAG, "Barrier fully opened by time out up");
    }
    else if (s_state == BARRIER_OPEN)
    {
        if (!barrier_read_loop())
        {
            s_state = BARRIER_CLOSING;
            barrier_set_relays(false, true);
            s_move_start_time = esp_timer_get_time();
            s_move_timeout = false;
            //s_position_percent = 100;
            xTimerChangePeriod(s_timer, pdMS_TO_TICKS(g_barrier_config.barrier_closing_ms), 0);
            xTimerStart(s_timer, 0);
            ESP_LOGI(TAG, "Barrier closing");
        }
        else
        {
            s_open_expire_time = esp_timer_get_time() + ((int64_t)g_barrier_config.barrier_open_ms * 1000);
            xTimerStart(s_timer, 0);
            ESP_LOGI(TAG, "Unable to close, loop occupied");
        }
    }
    else if (s_state == BARRIER_CLOSING)
    {
        s_state = BARRIER_IDLE;
        barrier_stop_relays();
        s_move_timeout = true;
        ESP_LOGI(TAG, "Barrier closed (timer)");
    }

    xSemaphoreGive(s_mutex);
}

void barrier_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex)
    {
        ESP_LOGE(TAG, "Failed to create barrier mutex");
        return;
    }

    s_timer = xTimerCreate(
        "barrier_timer",
        pdMS_TO_TICKS(g_barrier_config.barrier_opening_ms),
        pdFALSE,
        NULL,
        barrier_timer_cb);

    if (!s_timer)
    {
        ESP_LOGE(TAG, "Failed to create barrier timer");
        return;
    }

    barrier_stop_relays();
    ESP_LOGI(TAG, "Barrier control initialized, state=%d", s_state);
}

void barrier_trigger_open(void)
{
    if (!s_mutex || !s_timer)
    {
        ESP_LOGE(TAG, "barrier_trigger_open: not initialized");
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGE(TAG, "barrier_trigger_open: mutex timeout");
        return;
    }
    ESP_LOGW(TAG, "barrier_trigger_open: %d", s_state);

    s_error = false;
    s_error_msg[0] = '\0';

    if (!s_timer)
    {
        ESP_LOGE(TAG, "barrier_trigger_open: timer not initialized");
        xSemaphoreGive(s_mutex);
        return;
    }

    switch (s_state)
    {
    case BARRIER_ERROR:
        ESP_LOGE(TAG, "BARRIER IN STATE ERROR");

        break;
    case BARRIER_OPENING:
        //        xTimerReset(s_timer, 0);
        //        ESP_LOGI(TAG, "Barrier opening timer reset");
        break;

    case BARRIER_OPEN:
        s_open_expire_time = esp_timer_get_time() + ((int64_t)g_barrier_config.barrier_open_ms * 1000);
        xTimerReset(s_timer, 0);
        ESP_LOGI(TAG, "Barrier open, timer reset");
        break;
    default:
        /*
            ESP_LOGW(TAG, "state %d", s_state);
            s_open_expire_time = esp_timer_get_time() + ((int64_t)g_barrier_config.barrier_open_ms * 1000);
            xTimerStop(s_timer, 0);
            if (s_state != BARRIER_CLOSING)
                s_position_percent = 0;
            xTimerChangePeriod(s_timer, pdMS_TO_TICKS(g_barrier_config.barrier_opening_ms), 0);
            xTimerStart(s_timer, 0);
            ESP_LOGI(TAG, "Barrier opening from state: %d, possition %d\%  (0=IDLE 1=BARRIER_OPENING 3=CLOSING)", s_state, s_position_percent);
            s_state = BARRIER_OPENING;
            barrier_set_relays(true, false);
            s_move_start_time = esp_timer_get_time();
            s_move_timeout = false;
        */
        ESP_LOGW(TAG, "state %d", s_state);

        s_open_expire_time =
            esp_timer_get_time() +
            ((int64_t)g_barrier_config.barrier_open_ms * 1000);

        xTimerStop(s_timer, 0);

        uint32_t opening_time =
            s_time_up_ms ? s_time_up_ms : g_barrier_config.barrier_opening_ms;

        if (s_state == BARRIER_CLOSING)
        {
            uint32_t closing_time =
                s_time_down_ms ? s_time_down_ms : g_barrier_config.barrier_closing_ms;

            int64_t now = esp_timer_get_time();

            int64_t elapsed_close_ms =
                (now - s_move_start_time) / 1000;

            if (elapsed_close_ms < 0)
                elapsed_close_ms = 0;

            if (elapsed_close_ms > closing_time)
                elapsed_close_ms = closing_time;

            int64_t elapsed_open_ms =
                (elapsed_close_ms * opening_time) /
                closing_time;

            s_move_start_time =
                now - (elapsed_open_ms * 1000LL);

            uint32_t remaining_open_ms =
                opening_time - (uint32_t)elapsed_open_ms;

            if (remaining_open_ms < 100)
                remaining_open_ms = 100;

            xTimerChangePeriod(
                s_timer,
                pdMS_TO_TICKS(remaining_open_ms),
                0);
        }
        else
        {
            //s_position_percent = 0;
            s_move_start_time = esp_timer_get_time();

            xTimerChangePeriod(
                s_timer,
                pdMS_TO_TICKS(opening_time),
                0);
        }

        xTimerStart(s_timer, 0);

        s_state = BARRIER_OPENING;
        barrier_set_relays(true, false);
        s_move_timeout = false;

        break;
    }

    xSemaphoreGive(s_mutex);
}

void barrier_position_reached_up(void)
{
    if (!s_mutex || !s_timer)
    {
        ESP_LOGE(TAG, "barrier_position_reached_up: not initialized");
        return;
    }
    gpio_set_level(RELE3_GPIO, 1);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGE(TAG, "barrier_position_reached_up: mutex timeout");
        return;
    }

    s_error = false;
    s_error_msg[0] = '\0';

    if (s_state == BARRIER_CLOSING)
    {
        ESP_LOGE(TAG, "Unexpected finish up during closing");
    }
    else
    {
        int64_t now = esp_timer_get_time();
        int64_t elapsed_ms = (now - s_move_start_time) / 1000;
        if (elapsed_ms > 0 && elapsed_ms < 60000)
        {
            s_time_up_ms = (uint32_t)elapsed_ms;
        }
        s_state = BARRIER_OPEN;
        s_open_expire_time = esp_timer_get_time() + ((int64_t)g_barrier_config.barrier_open_ms * 1000);
        //s_position_percent = 100;
        barrier_stop_relays();
        if (s_timer)
        {
            xTimerStop(s_timer, 0);

            xTimerChangePeriod(
                s_timer,
                pdMS_TO_TICKS(g_barrier_config.barrier_open_ms),
                0);

            xTimerReset(s_timer, 0);
        }
        ESP_LOGI(TAG, "Barrier fully opened by finish up switch, time_up_ms=%u", s_time_up_ms);
    }

    xSemaphoreGive(s_mutex);
}

void barrier_position_reached_down(void)
{
    if (!s_mutex || !s_timer)
    {
        ESP_LOGE(TAG, "barrier_position_reached_down: not initialized");
        return;
    }
    gpio_set_level(RELE3_GPIO, 0);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGE(TAG, "barrier_position_reached_down: mutex timeout");
        return;
    }

    s_error = false;
    s_error_msg[0] = '\0';

    if (s_state == BARRIER_CLOSING)
    {
        int64_t now = esp_timer_get_time();
        int64_t elapsed_ms = (now - s_move_start_time) / 1000;
        if (elapsed_ms > 0 && elapsed_ms < 60000)
        {
            s_time_down_ms = (uint32_t)elapsed_ms;
        }
        s_state = BARRIER_IDLE;
        //s_position_percent = 0;
        barrier_stop_relays();
        if (s_timer)
        {
            xTimerStop(s_timer, 0);
        }
        ESP_LOGI(TAG, "Barrier fully closed by finish down switch, time_down_ms=%u, set s_position_percent = 0", s_time_down_ms);
    }
    else if (s_state == BARRIER_OPENING)
    {
        ESP_LOGE(TAG, "Unexpected finish down during opening");
    }
    else
    {
        s_state = BARRIER_IDLE;
        //s_position_percent = 0;
        ESP_LOGI(TAG, "Rich Down s_position_percent = 0");
        barrier_stop_relays();
    }

    xSemaphoreGive(s_mutex);
}

static int barrier_calculate_position(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGE(TAG, "barrier_calculate_position: mutex timeout");
        return 0;
    }

    int pos = 0;

    if (s_state == BARRIER_IDLE || s_state == BARRIER_ERROR)
    {
        pos = 0;
    }
    else if (s_state == BARRIER_OPEN)
    {
        pos = 100;
    }
    else
    {
        int64_t now = esp_timer_get_time();
        int64_t elapsed_ms = (now - s_move_start_time) / 1000;

        if (s_state == BARRIER_OPENING)
        {
            uint32_t move_time =
                s_time_up_ms ? s_time_up_ms : g_barrier_config.barrier_opening_ms;

            pos = (int)((elapsed_ms * 100) / (int64_t)move_time);
            if (pos > 100)
                pos = 100;
        }
        else if (s_state == BARRIER_CLOSING)
        {
            uint32_t move_time =
                s_time_down_ms ? s_time_down_ms : g_barrier_config.barrier_closing_ms;

            pos = 100 - (int)((elapsed_ms * 100) / (int64_t)move_time);
            if (pos < 0)
                pos = 0;
        }
    }

    xSemaphoreGive(s_mutex);
    return pos;
}

static void barrier_update_status(int loop, int finish_up, int finish_down, char *buf, size_t len, uint32_t *remaining_ms, bool *status_changed)
{
    if (status_changed)
    {
        *status_changed = false;
    }

    if (remaining_ms)
    {
        *remaining_ms = 0;
    }

    if (!s_mutex)
    {
        snprintf(buf, len, "Uninitialized");
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        snprintf(buf, len, "Busy");
        return;
    }

    if (s_error)
    {
        snprintf(buf, len, "%s", s_error_msg);
        xSemaphoreGive(s_mutex);
        return;
    }

    bool loop_active = g_barrier_config.loop_active_high ? (loop == 1) : (loop == 0);

    bool fin_up_active = (finish_up == 0);
    bool fin_dn_active = (finish_down == 0);
    /*
        if (fin_up_active && fin_dn_active)
        {
            barrier_set_error("Fault: both limit switches active");
            snprintf(buf, len, "%s", s_error_msg);
            xSemaphoreGive(s_mutex);
            return;
        }
    */
    int64_t now = esp_timer_get_time();
    int64_t elapsed_ms = (now - s_move_start_time) / 1000;

    bool stop_motors = false;
    char stop_msg[80] = "";

    switch (s_state)
    {
    case BARRIER_IDLE:
        if (loop_active)
        {
            snprintf(buf, len, "Idle (loop occupied)");
        }
        else
        {
            snprintf(buf, len, "Idle (%s)", (s_move_timeout) ? "Timer" : "Finish");
        }
        break;

    case BARRIER_OPENING:
        if (fin_up_active)
        {
            snprintf(buf, len, "Opening");
        }
        else if (elapsed_ms > (int64_t)g_barrier_config.barrier_opening_ms + 2000)
        {
            stop_motors = true;
            snprintf(stop_msg, sizeof(stop_msg), "Stuck: opening timeout");
            snprintf(buf, len, "%s", stop_msg);
        }
        else
        {
            snprintf(buf, len, "Moving up");
        }
        break;

    case BARRIER_OPEN:
        if (loop_active)
        {
            snprintf(buf, len, "Open - loop occupied, waiting");
        }
        else
        {
            int64_t remaining = (s_open_expire_time - esp_timer_get_time()) / 1000;
            if (remaining < 0)
                remaining = 0;
            if (remaining_ms)
                *remaining_ms = (uint32_t)remaining;
            snprintf(buf, len, "Open (%s) (hold for %ds)", (s_move_timeout) ? "Timer" : "Finish", (int)((remaining + 999) / 1000));
        }
        break;

    case BARRIER_CLOSING:
        if (fin_dn_active)
        {
            snprintf(buf, len, "Closing");
        }
        else if (loop_active)
        {
            snprintf(buf, len, "Closing blocked (obstacle)");
            // barrier_handle_obstacle_during_closing();
        }
        else if (elapsed_ms > (int64_t)g_barrier_config.barrier_closing_ms + 2000)
        {
            stop_motors = true;
            snprintf(stop_msg, sizeof(stop_msg), "Stuck: closing timeout");
            snprintf(buf, len, "%s", stop_msg);
        }
        else
        {
            snprintf(buf, len, "Moving down");
        }
        break;

    case BARRIER_ERROR:
        snprintf(buf, len, "%s", s_error_msg);
        break;

    default:
        snprintf(buf, len, "Unknown");
        break;
    }

    if (stop_motors)
    {
        barrier_set_error(stop_msg);
        if (status_changed)
            *status_changed = true;
    }

    xSemaphoreGive(s_mutex);
}

void barrier_task(void *arg)
{
    (void)arg;

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
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io);

    gpio_config_t io_out = {
        .pin_bit_mask = (1ULL << RELE3_GPIO) | (1ULL << RELE2_GPIO) | (1ULL << RELE1_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_out);

    gpio_set_level(RELE1_GPIO, 0);
    gpio_set_level(RELE2_GPIO, 0);
    gpio_set_level(RELE3_GPIO, 0);

    uint8_t last_rex1 = -1;
    uint8_t last_rex2 = -1;
    uint8_t last_loop = -1;
    uint8_t last_finish_up = -1;
    uint8_t last_finish_down = -1;
    uint8_t last_rele1 = -1;
    uint8_t last_rele2 = -1;
    uint8_t last_rele3 = -1;
    uint8_t last_position = 0;
    uint32_t last_remaining_ms = 0;

    barrier_init();

    while (1)
    {
        uint32_t time_up_ms = s_time_up_ms ? s_time_up_ms : g_barrier_config.barrier_opening_ms;

        uint32_t time_down_ms = s_time_down_ms ? s_time_down_ms : g_barrier_config.barrier_closing_ms;

        int rex1 = gpio_get_level(REX1_GPIO);
        int rex2 = gpio_get_level(REX2_GPIO);
        int loop = gpio_get_level(LOOP_GPIO);
        int finish_up = gpio_get_level(FINISH_UP_GPIO);
        int finish_down = gpio_get_level(FINISH_DOWN_GPIO);
        int rele1 = gpio_get_level(RELE1_GPIO);
        int rele2 = gpio_get_level(RELE2_GPIO);
        int rele3 = gpio_get_level(RELE3_GPIO);

        if (s_relay_switch_pending)
        {
            int64_t now = esp_timer_get_time();
            int64_t elapsed_since_switch = (now - s_relay_switch_time) / 1000;
            if (elapsed_since_switch >= BARRIER_SWITCH_DELAY_MS)
            {
                if (s_state == BARRIER_OPENING && !s_last_up)
                {
                    gpio_set_level(RELE1_GPIO, 1);
                    s_last_up = true;
                }
                else if (s_state == BARRIER_CLOSING && !s_last_down)
                {
                    gpio_set_level(RELE2_GPIO, 1);
                    s_last_down = true;
                }
                s_relay_switch_pending = false;
            }
        }

        bool changed = false;

        if (rex1 != last_rex1)
        {
            if (!rex1)
            {
                barrier_trigger_open();
                ESP_LOGI(TAG, "REX1 activated relay %d", g_config.rex1_relay_number);
            }
            dispatch_log_event(6, 1, rex1, 0);
            last_rex1 = rex1;
            changed = true;
        }

        if (rex2 != last_rex2)
        {
            if (!rex2)
            {
                barrier_trigger_open();
                ESP_LOGI(TAG, "REX2 activated relay %d", g_config.rex2_relay_number);
            }
            dispatch_log_event(6, 2, rex2, 0);
            last_rex2 = rex2;
            changed = true;
        }

        if (loop != last_loop)
        {
            bool loop_triggered = g_barrier_config.loop_active_high ? (loop == 1) : (loop == 0);
            if (loop_triggered && finish_down) // SI DETECTA ALGO EN EL LOOP y la barrera no está en IDLE
            {
                barrier_trigger_open();
                ESP_LOGI(TAG, "Loop detector triggered finish_down:%d", finish_down);
            }

            if (!loop_triggered)
            {
                s_open_expire_time = esp_timer_get_time() + ((int64_t)g_barrier_config.barrier_open_ms * 1000);
                xTimerReset(s_timer, 0);
            }
            last_loop = loop;
            changed = true;
        }

        if ((finish_up != last_finish_up || finish_down != last_finish_down) && s_state == BARRIER_ERROR)
        {
            if (finish_up != last_finish_up)
                last_finish_down = -1;
            if (finish_down != last_finish_down)
                last_finish_up = -1;
        }

        if (finish_up != last_finish_up)
        {
            if (!finish_up && finish_down)
            {
                barrier_position_reached_up();
                ESP_LOGI(TAG, "Finish up triggered");
            }

            if (!finish_up && !finish_down)
            {
                ESP_LOGE(TAG, "Startup: both limit switches active - fault");
                barrier_set_error("Fault: both limit switches active");
            }

            last_finish_up = finish_up;
            changed = true;
        }

        if (finish_down != last_finish_down)
        {
            if (!finish_down && finish_up)
            {
                barrier_position_reached_down();
                ESP_LOGI(TAG, "Finish downtriggered");
            }

            if (!finish_up && !finish_down)
            {
                ESP_LOGE(TAG, "Startup: both limit switches active - fault");
                barrier_set_error("Fault: both limit switches active");
            }

            last_finish_down = finish_down;
            changed = true;
        }

        if (rele1 != last_rele1 || rele2 != last_rele2 || rele3 != last_rele3)
        {
            last_rele1 = rele1;
            last_rele2 = rele2;
            last_rele3 = rele3;
            changed = true;
        }
 
        uint8_t s_position_percent = barrier_calculate_position();

        int pos_diff = s_position_percent - last_position;
        if (pos_diff < 0)
            pos_diff = -pos_diff;
        if (pos_diff >= 5)
        {
            last_position = s_position_percent;

            changed = true;
        }

        char status_buf[64];
        uint32_t remaining_ms = 0;
        bool status_changed = false;
        barrier_update_status(loop, finish_up, finish_down, status_buf, sizeof(status_buf), &remaining_ms, &status_changed);

        if (changed || status_changed || (remaining_ms / 1000) != (last_remaining_ms / 1000))
        {
            last_remaining_ms = remaining_ms;
            ws_broadcast_barrier(rex1, rex2, loop, finish_up, finish_down, rele1, rele2, rele3,
                                 time_up_ms, time_down_ms, g_barrier_config.barrier_open_ms, s_position_percent, g_barrier_config.loop_active_high,
                                 status_buf, remaining_ms);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void force_broadcast_barrier(void)
{
    int _loop = gpio_get_level(LOOP_GPIO);
    int _finish_up = gpio_get_level(FINISH_UP_GPIO);
    int _finish_down = gpio_get_level(FINISH_DOWN_GPIO);
    char status_buf[64];
    uint32_t remaining_ms = 0;
    barrier_update_status(_loop, _finish_up, _finish_down, status_buf, sizeof(status_buf), &remaining_ms, NULL);
    ws_broadcast_barrier(gpio_get_level(REX1_GPIO), gpio_get_level(REX2_GPIO), _loop, _finish_up, _finish_down, gpio_get_level(RELE1_GPIO), gpio_get_level(RELE2_GPIO), gpio_get_level(RELE3_GPIO), s_time_up_ms, s_time_down_ms, g_barrier_config.barrier_open_ms, 0, g_barrier_config.loop_active_high, status_buf, remaining_ms);
}
