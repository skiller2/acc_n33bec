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
    gpio_num_t rex1_relay_gpio;        // GPIO number for REX1 relay
    gpio_num_t rex2_relay_gpio;        // GPIO number for REX2 relay
    gpio_num_t port1_relay_gpio;     // GPIO number for port 1 relay
    gpio_num_t port2_relay_gpio;     // GPIO number for port 2 relay
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

esp_err_t config_load(config_t *config);
esp_err_t config_save(const config_t *config);
static config_t g_config = {0};

#ifdef __cplusplus
}
#endif
