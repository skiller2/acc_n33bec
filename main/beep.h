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

typedef struct {
    int freq;
    int duration;
    int pause;
} tone_t;

void play_melody(gpio_num_t gpio, const tone_t *melody, int length, float incdur);
void pulse_output(gpio_num_t gpio, uint32_t duration_ms);

void play_melody_async(gpio_num_t gpio,const tone_t *melody, int length, float incdur);

#ifdef __cplusplus
}
#endif
