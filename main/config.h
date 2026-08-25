#pragma once

#include <stdint.h>
#include <esp_err.h>
#include <soc/gpio_num.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t magic;
    uint8_t version;    
    uint8_t rex1_relay_number;
    uint8_t rex2_relay_number;
    uint8_t port1_relay_number;
    uint8_t port2_relay_number;
    uint32_t rex1_relay_duration_ms;
    uint32_t rex2_relay_duration_ms;
    uint32_t port1_relay_duration_ms;
    uint32_t port2_relay_duration_ms;
    uint32_t input_debounce_ms;
    uint32_t device_id;                     // Unique device ID for this access control unit
    char url_n33bec[256]; // URL for N33-BEC server
    char cod_tema[256]; // URL for N33-BEC server
    uint32_t keep_alive_secs; // Keep alive interval in seconds
} config_t;

static inline gpio_num_t relay_number_to_gpio(uint8_t relay_number)
{
    switch (relay_number)
    {
        case 1: return GPIO_NUM_45;
        case 2: return GPIO_NUM_39;
        case 3: return GPIO_NUM_33;
        default: return GPIO_NUM_45;
    }
}

esp_err_t config_load(config_t *config);
esp_err_t config_save(const config_t *config);
static config_t g_config = {0};

#ifdef __cplusplus
}
#endif
