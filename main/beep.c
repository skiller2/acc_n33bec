#include "beep.h"
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/ledc.h>

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

void pulse_output(gpio_num_t gpio, uint32_t duration_ms)
{

    static TimerHandle_t timer = NULL;
    // configure GPIO
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "pulse_output: GPIO %d ON for %u ms", gpio, duration_ms);


    if (duration_ms == 0) {
        gpio_set_level(gpio, 0);
        return;
    }

    // set HIGH immediately
    gpio_set_level(gpio, 1);

    // create one-shot timer

    if (timer != NULL)
        xTimerDelete(timer, 0);

    timer = xTimerCreate(
        "pulse_timer",
        pdMS_TO_TICKS(duration_ms),
        pdFALSE,        // one-shot
        (void *)gpio,   // store gpio in timer
        output_off_cb); //  callback when timer expires

    if (timer != NULL)
        xTimerStart(timer, 0);
}

static void melody_task(void *arg)
{
    melody_ctx_t ctx;
    memcpy(&ctx, (melody_ctx_t *)arg, sizeof(ctx));
    ESP_LOGI(TAG, "melody start");

    // melody_ctx_t *ctx = (melody_ctx_t *)arg;
    uint32_t ulNotificationValue;
    gpio_set_direction(ctx.gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(ctx.gpio, 0);

    for (int i = 0; i < ctx.length; i++)
    {
        if (ctx.melody[i].freq > 0)
        {
            // heap_caps_check_integrity_all(true);
            ledc_timer_config_t timer = {
                .duty_resolution = LEDC_TIMER_10_BIT,
                .freq_hz = ctx.melody[i].freq,
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .timer_num = LEDC_TIMER_0,
                .clk_cfg = LEDC_AUTO_CLK};
            ledc_timer_config(&timer);
            ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1);

            // Configure channel
            ledc_channel_config_t channel = {
                .gpio_num = ctx.gpio,
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .channel = LEDC_CHANNEL_0,
                .intr_type = LEDC_INTR_DISABLE,
                .timer_sel = LEDC_TIMER_0,
                .duty = 900, // louder than 512
                .hpoint = 0};
            ledc_channel_config(&channel);

            // Play tone
            ulNotificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ctx.melody[i].duration * ctx.incdur));
            if (ulNotificationValue > 0)
            {
                ESP_LOGI(TAG, "kill my self");
                break;
            }
            // Stop tone
            ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1);
            gpio_reset_pin(ctx.gpio);
        }
        ulNotificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ctx.melody[i].pause * ctx.incdur));
        if (ulNotificationValue > 0)
        {
            ESP_LOGI(TAG, "kill my self");
            break;
        }
    }
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1);
    gpio_reset_pin(ctx.gpio);
    ESP_LOGI(TAG, "melody finish");
    vTaskDelete(NULL); // kill task when done
    vTaskSuspend(NULL);
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