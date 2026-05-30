#pragma once

#include <stdint.h>
#include <esp_err.h>
#include <soc/gpio_num.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t rex1_relay_gpio;        // GPIO number for REX1 relay
    gpio_num_t rex2_relay_gpio;        // GPIO number for REX2 relay
    gpio_num_t reader1_relay_gpio;     // GPIO number for reader 1 relay
    gpio_num_t reader2_relay_gpio;     // GPIO number for reader 2 relay
    uint32_t rex1_relay_duration_ms;
    uint32_t rex2_relay_duration_ms;
    uint32_t reader1_relay_duration_ms;
    uint32_t reader2_relay_duration_ms;
} config_t;

esp_err_t config_load(config_t *config);
esp_err_t config_save(const config_t *config);

#ifdef __cplusplus
}
#endif
