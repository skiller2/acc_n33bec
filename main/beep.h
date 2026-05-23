#pragma once

#include <driver/gpio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Activate a GPIO pin for a specified duration (beep/buzzer pulse)
 *
 * @param gpio GPIO pin number
 * @param duration_ms Duration in milliseconds
 */
void beep(gpio_num_t gpio, uint32_t duration_ms);
void led(gpio_num_t gpio, uint32_t duration_ms);

typedef struct {
    int freq;
    int duration;
    int pause;
} tone_t;


#ifdef __cplusplus
}
#endif
