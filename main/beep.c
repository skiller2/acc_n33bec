#include "beep.h"
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/ledc.h>

static const char *TAG = "beep";

void beep_tone(gpio_num_t gpio, int freq_hz, int duration_ms)
{
    ledc_timer_config_t timer = {
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = freq_hz,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num = gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 512
    };
    ledc_channel_config(&channel);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
}

void play_melody(gpio_num_t gpio, tone_t *melody, int length)
{
    for (int i = 0; i < length; i++) {
        beep_tone(gpio, melody[i].freq, melody[i].duration);
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
        {1000, 200, 50},
        {1500, 200, 50},
        {2000, 300, 100},
    };

    play_melody(gpio, melody, 3);

    /*
    // Set GPIO high
    gpio_set_level(gpio, 1);
    ESP_LOGI(TAG, "beep: GPIO %d ON for %u ms", gpio, duration_ms);

    // Wait for specified duration
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    // Set GPIO low
    gpio_set_level(gpio, 0);
    ESP_LOGI(TAG, "beep: GPIO %d OFF", gpio);
    */
}

void led(gpio_num_t gpio, uint32_t duration_ms)
{
    if (duration_ms == 0) {
        ESP_LOGW(TAG, "led: duration is 0");
        return;
    }

    // Ensure GPIO is set as output
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);

    // Set GPIO high
    gpio_set_level(gpio, 1);
    ESP_LOGI(TAG, "led: GPIO %d ON for %u ms", gpio, duration_ms);

    // Wait for specified duration
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    // Set GPIO low
    gpio_set_level(gpio, 0);
    ESP_LOGI(TAG, "led: GPIO %d OFF", gpio);
}
