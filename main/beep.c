#include "beep.h"
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/ledc.h>

static const char *TAG = "beep";


typedef struct {
    gpio_num_t gpio;
    const tone_t *melody;
    int length;
    float incdur;
} melody_ctx_t;

void beep_tone(gpio_num_t gpio, int freq_hz, int duration_ms)
{
    
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1);

    // Configure timer EVERY TIME (this is key for your hardware)
    ledc_timer_config_t timer = {
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = freq_hz,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    // Configure channel
    ledc_channel_config_t channel = {
        .gpio_num = gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 900,  // louder than 512
        .hpoint = 0
    };
    ledc_channel_config(&channel);

    // Play tone
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    // Stop tone
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1);
    gpio_reset_pin(gpio);
    
}


void play_melody(gpio_num_t gpio, const tone_t *melody, int length, float incdur)
{
    for (int i = 0; i < length; i++) {

        if (melody[i].freq > 0) {
            beep_tone(gpio, melody[i].freq, melody[i].duration * incdur);
        } else {
            // rest
            vTaskDelay(pdMS_TO_TICKS(melody[i].duration));
        }

        vTaskDelay(pdMS_TO_TICKS(melody[i].pause));
    }
}


void beep(gpio_num_t gpio, uint32_t duration_ms)
{
    if (duration_ms == 0) {
        ESP_LOGW(TAG, "beep: duration is 0");
        return;
    }

    // Ensure GPIO is set as output
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);

    tone_t melody[] = {
        {1200, 120, 30},
        {1600, 120, 30},
        {2000, 180, 60},
        {0,    50,  0},   // pause
        {1800, 250, 0}
    };

    play_melody(gpio, melody, sizeof(melody) / sizeof(tone_t),1);

/*    
    // Set GPIO high
    gpio_set_level(gpio, 0);
    ESP_LOGI(TAG, "beep: GPIO %d ON for %u ms", gpio, duration_ms);

    // Wait for specified duration
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    // Set GPIO low
    gpio_set_level(gpio, 1);
    ESP_LOGI(TAG, "beep: GPIO %d OFF", gpio);
  */  
   //gpio_set_level(gpio, 1);
}


static void output_off_cb(TimerHandle_t xTimer)
{
    gpio_num_t gpio = (gpio_num_t) pvTimerGetTimerID(xTimer);

    gpio_set_level(gpio, 0);
}


void pulse_output(gpio_num_t gpio, uint32_t duration_ms)
{
    // configure GPIO
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);

    // set HIGH immediately
    gpio_set_level(gpio, 1);

    // create one-shot timer
    TimerHandle_t timer = xTimerCreate(
        "pulse_timer",
        pdMS_TO_TICKS(duration_ms),
        pdFALSE,                  // one-shot
        (void*) gpio,             // store gpio in timer
        output_off_cb
    );

    if (timer != NULL) {
        xTimerStart(timer, 0);
    }
}

static void melody_task(void *arg)
{
    melody_ctx_t *ctx = (melody_ctx_t *)arg;

    for (int i = 0; i < ctx->length; i++) {

        if (ctx->melody[i].freq > 0) {
            beep_tone(ctx->gpio,
                      ctx->melody[i].freq,
                      ctx->melody[i].duration * ctx->incdur);
        } else {
            vTaskDelay(pdMS_TO_TICKS(ctx->melody[i].duration));
        }

        vTaskDelay(pdMS_TO_TICKS(ctx->melody[i].pause));
    }

    // cleanup
    free(ctx);

    vTaskDelete(NULL); // kill task when done
}

void play_melody_async(gpio_num_t gpio,
                       const tone_t *melody,
                       int length,
                       float incdur)
{
    melody_ctx_t *ctx = malloc(sizeof(melody_ctx_t));

    ctx->gpio = gpio;
    ctx->melody = melody;
    ctx->length = length;
    ctx->incdur = incdur;

    xTaskCreate(
        melody_task,
        "melody_task",
        2048,
        ctx,
        5,
        NULL
    );
}