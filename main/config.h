#pragma once

#include <stdint.h>
#include <esp_err.h>
#include <soc/gpio_num.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOOR1_GPIO GPIO_NUM_21
#define DOOR2_GPIO GPIO_NUM_17
#define REX1_GPIO GPIO_NUM_16
#define REX2_GPIO GPIO_NUM_18
#define BAT_GPIO GPIO_NUM_34 // VERDE (Salida 12v)
#define CAR_GPIO GPIO_NUM_35 // AMARILLO (Pulso al morir)
#define ALI_GPIO GPIO_NUM_36 // ROJO Encendido tiene 220

#define LOOP_GPIO GPIO_NUM_36 // Espira / loop detector
#define FINISH_UP_GPIO GPIO_NUM_21 // Mechanical finish up switch
#define FINISH_DOWN_GPIO GPIO_NUM_17 // Mechanical finish down switch

#define PORT1_BUZZER GPIO_NUM_46
#define PORT2_BUZZER GPIO_NUM_40

#define RELE1_GPIO GPIO_NUM_45
#define RELE2_GPIO GPIO_NUM_39
#define RELE3_GPIO GPIO_NUM_33

typedef struct {
    uint32_t magic;
    uint8_t version;    
    uint8_t rex1_relay_number;
    uint8_t rex2_relay_number;
    uint8_t port1_relay_number;
    uint8_t port1_relay2_number;
    uint8_t port2_relay_number;
    uint8_t port2_relay2_number;
    uint32_t rex1_relay_duration_ms;
    uint32_t rex2_relay_duration_ms;
    uint32_t port1_relay_duration_ms;
    uint32_t port1_relay2_duration_ms;
    uint32_t port2_relay_duration_ms;
    uint32_t port2_relay2_duration_ms;
    uint32_t input_debounce_ms;
    uint32_t device_id;                     // Unique device ID for this access control unit
    char url_n33bec[256]; // URL for N33-BEC server
    char cod_tema[256]; // URL for N33-BEC server
    uint32_t keep_alive_secs; // Keep alive interval in seconds
} config_t;

typedef struct {
    uint32_t magic;
    uint8_t version;    
    uint32_t barrier_opening_ms;
    uint32_t barrier_closing_ms;
    uint32_t barrier_open_ms;
    uint8_t loop_active_high;  /* 0 = LOW sensed as object, 1 = HIGH sensed as object */
} barrier_config_t;

esp_err_t config_load(config_t *config);
esp_err_t config_save(const config_t *config);
esp_err_t barrier_config_load(barrier_config_t *config);
esp_err_t barrier_config_save(const barrier_config_t *config);
extern barrier_config_t g_barrier_config;
static config_t g_config = {0};

#ifdef __cplusplus
}
#endif
