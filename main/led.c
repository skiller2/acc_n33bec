#include "driver/gpio.h"
#include "esp_system.h"
#include "led_strip.h"
#include "esp_log.h"
#include "config.h"

static led_strip_handle_t led_strip; // LED strip handle
static const char *TAG = "led";

void init_led()
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = GPIO_NUM_21,                               // Set GPIO pin
        .max_leds = 1,                                               // Set number of LEDs
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB, // Set color format
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // RMT resolution, 10MHz
        .flags.with_dma = false,           // Disable DMA
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);                  // Clear the strip to off initially
}

void set_led_color(uint32_t red, uint32_t green, uint32_t blue)
{
    return;
    led_strip_set_pixel(led_strip, 0, green,red, blue);
    led_strip_refresh(led_strip);
    ESP_LOGW(TAG, "REFRESH LED COLOR: R=%d G=%d B=%d", red, green, blue);
}