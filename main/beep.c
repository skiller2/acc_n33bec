#include "beep.h"
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/ledc.h>
#include <soc/gpio_num.h>
#include "config.h"

static const char *TAG = "melody";

typedef struct
{
    gpio_num_t gpio;
    const tone_t *melody;
    int length;
    float incdur;
} melody_ctx_t;

static void output_off_cb(TimerHandle_t xTimer)
{
    gpio_num_t gpio = (gpio_num_t)pvTimerGetTimerID(xTimer); // get the gpio from timer

    gpio_set_level(gpio, 0);
}

static TimerHandle_t gpio_timers[GPIO_NUM_MAX] = {0};

void pulse_output(gpio_num_t gpio, uint32_t duration_ms)
{
    if (duration_ms == 0)
    {
        gpio_set_level(gpio, 0);
        return;
    }

    gpio_set_level(gpio, 1);

    if (gpio_timers[gpio] == NULL)
    {
        gpio_timers[gpio] = xTimerCreate(
            "pulse",
            pdMS_TO_TICKS(duration_ms),
            pdFALSE,
            (void *)gpio,
            output_off_cb);
    }

    if (gpio_timers[gpio])
    {
        xTimerStop(gpio_timers[gpio], 0);
        xTimerChangePeriod(
            gpio_timers[gpio],
            pdMS_TO_TICKS(duration_ms),
            0);
    }
}

void pulse_output_by_relay(uint8_t relay_number, uint32_t duration_ms)
{
    if (relay_number < 1 || relay_number > 3)
        return;

    gpio_num_t gpio;
    switch (relay_number)
    {
    case 1:
        gpio = RELE1_GPIO;
        break;
    case 2:
        gpio = RELE2_GPIO;
        break;
    case 3:
        gpio = RELE3_GPIO;
        break;
    default:
        return;
    }

    heap_caps_check_integrity_all(true);
    pulse_output(gpio, duration_ms);
    heap_caps_check_integrity_all(true);
}

static void melody_task(void *arg)
{
    melody_ctx_t ctx;
    memcpy(&ctx, arg, sizeof(ctx));

    ESP_LOGI(TAG, "melody start");

    uint32_t ulNotificationValue;

    ledc_channel_config_t channel = {
        .gpio_num = ctx.gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };

    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK};

    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    for (int i = 0; i < ctx.length; i++)
    {
        if (ctx.melody[i].freq > 0)
        {
            ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, ctx.melody[i].freq);

            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 900);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

            ulNotificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ctx.melody[i].duration * ctx.incdur));

            if (ulNotificationValue)
            {
                ESP_LOGI(TAG, "melody cancelled");
                break;
            }

            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1023);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            ulNotificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ctx.melody[i].pause));
            if (ulNotificationValue)
            {
                ESP_LOGI(TAG, "melody cancelled");
                break;
            }
        }
    }

    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1);

    ESP_LOGI(TAG, "melody finish");
    gpio_reset_pin(ctx.gpio);

    gpio_set_level(PORT1_BUZZER, 1);
    gpio_set_level(PORT2_BUZZER, 1);

    vTaskDelete(NULL);
}

void play_melody_async(gpio_num_t gpio,
                       const tone_t *melody,
                       int length,
                       float incdur)
{
    static TaskHandle_t melody_task_handle = NULL;
    static melody_ctx_t ctx;

    if (melody_task_handle)
    {
        if (eTaskGetState(melody_task_handle) == eRunning || eTaskGetState(melody_task_handle) == eBlocked)
        {
            xTaskNotifyGive(melody_task_handle);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        for (int i = 0; i < 50; i++)
        {
            if (eTaskGetState(melody_task_handle) == eDeleted || eTaskGetState(melody_task_handle) == eReady)
            {
                melody_task_handle = NULL;
                break;
            }
            ESP_LOGI(TAG, "wait for stop: retry %d, eTaskGetState: %d", i, eTaskGetState(melody_task_handle));

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    ctx.gpio = gpio;
    ctx.melody = melody;
    ctx.length = length;
    ctx.incdur = incdur;

    xTaskCreate(
        melody_task,
        "melody_task",
        2048,
        &ctx,
        5,
        &melody_task_handle);
}