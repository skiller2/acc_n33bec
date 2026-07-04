#include "config.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#define RELE1_GPIO GPIO_NUM_45  //Reader 1 LED
#define RELE2_GPIO GPIO_NUM_39  //Reader 2 LED
#define RELE3_GPIO GPIO_NUM_33  //Reader 3 LED


static const char *TAG = "config";
static const char *CONFIG_PATH = "/fs/config.dat";
static const uint32_t CONFIG_MAGIC = 0x434F4E46; // 'CONF'
static const uint8_t CONFIG_VERSION = 1;

typedef struct {
    uint32_t magic;
    uint8_t version;
    gpio_num_t rex1_relay_gpio;
    gpio_num_t rex2_relay_gpio;
    gpio_num_t reader1_relay_gpio;
    gpio_num_t reader2_relay_gpio;
    uint32_t rex1_relay_duration_ms;
    uint32_t rex2_relay_duration_ms;
    uint32_t reader1_relay_duration_ms;
    uint32_t reader2_relay_duration_ms;
    uint32_t input_debounce_ms;
} stored_config_t;

static void set_defaults(config_t *config)
{
    config->rex1_relay_gpio = RELE1_GPIO;
    config->rex2_relay_gpio = RELE2_GPIO;
    config->reader1_relay_gpio = RELE1_GPIO;
    config->reader2_relay_gpio = RELE3_GPIO;
    config->rex1_relay_duration_ms = 2000;
    config->rex2_relay_duration_ms = 2000;
    config->reader1_relay_duration_ms = 2000;
    config->reader2_relay_duration_ms = 2000;
    config->input_debounce_ms = 100;
}

static bool valid_relay_number(gpio_num_t relay)
{
    if (relay == RELE1_GPIO || relay == RELE2_GPIO || relay == RELE3_GPIO) {
        return true;
    }
    return false;
}

static void clamp_config(config_t *config)
{
    if (!valid_relay_number(config->rex1_relay_gpio)) {
        config->rex1_relay_gpio = RELE1_GPIO;
    }
    if (!valid_relay_number(config->rex2_relay_gpio)) {
        config->rex2_relay_gpio  = RELE2_GPIO;
    }
    if (!valid_relay_number(config->reader1_relay_gpio)) {
        config->reader1_relay_gpio = RELE1_GPIO;
    }
    if (!valid_relay_number(config->reader2_relay_gpio)) {
        config->reader2_relay_gpio = RELE3_GPIO;
    }
    if (config->rex1_relay_duration_ms == 0) {
        config->rex1_relay_duration_ms = 2000;
    }
    if (config->rex2_relay_duration_ms == 0) {
        config->rex2_relay_duration_ms = 2000;
    }
    if (config->reader1_relay_duration_ms == 0) {
        config->reader1_relay_duration_ms = 2000;
    }
    if (config->reader2_relay_duration_ms == 0) {
        config->reader2_relay_duration_ms = 2000;
    }
    if (config->input_debounce_ms == 0) {
        config->input_debounce_ms = 100;
    }
}

esp_err_t config_save(const config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    stored_config_t stored = {
        .magic = CONFIG_MAGIC,
        .version = CONFIG_VERSION,
        .rex1_relay_gpio = config->rex1_relay_gpio,
        .rex2_relay_gpio = config->rex2_relay_gpio,
        .rex1_relay_duration_ms = config->rex1_relay_duration_ms,
        .rex2_relay_duration_ms = config->rex2_relay_duration_ms,
        .reader1_relay_gpio = config->reader1_relay_gpio,
        .reader2_relay_gpio = config->reader2_relay_gpio,
        .reader1_relay_duration_ms = config->reader1_relay_duration_ms,
        .reader2_relay_duration_ms = config->reader2_relay_duration_ms,
        .input_debounce_ms = config->input_debounce_ms,
    };

    FILE *f = fopen(CONFIG_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open config file for write: %s", CONFIG_PATH);
        return ESP_FAIL;
    }

    size_t written = fwrite(&stored, 1, sizeof(stored), f);
    fclose(f);

    if (written != sizeof(stored)) {
        ESP_LOGE(TAG, "Failed to write full config file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved Reader config: reader1_relay=%u reader1_ms=%u reader2_relay=%u reader2_ms=%u",
             stored.reader1_relay_gpio,
             stored.reader1_relay_duration_ms,
             stored.reader2_relay_gpio,
             stored.reader2_relay_duration_ms);


    ESP_LOGI(TAG, "Saved REX config: rex1_relay=%u rex1_ms=%u rex2_relay=%u rex2_ms=%u",
             stored.rex1_relay_gpio,
             stored.rex1_relay_duration_ms,
             stored.rex2_relay_gpio,
             stored.rex2_relay_duration_ms);
    //TODO: reload config into RAM struct to ensure it's valid and clamp values if needed
    config_load(&g_config);

    return ESP_OK;
}

esp_err_t config_load(config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    set_defaults(config);

    FILE *f = fopen(CONFIG_PATH, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Config file not found, using defaults");
        return config_save(config);
    }

    stored_config_t stored;
    size_t read = fread(&stored, 1, sizeof(stored), f);
    fclose(f);

    if (read != sizeof(stored) || stored.magic != CONFIG_MAGIC || stored.version != CONFIG_VERSION) {
        ESP_LOGW(TAG, "Invalid config file or unsupported version, using defaults");
        return config_save(config);
    }

    config->rex1_relay_gpio = stored.rex1_relay_gpio;
    config->rex2_relay_gpio = stored.rex2_relay_gpio;
    config->rex1_relay_duration_ms = stored.rex1_relay_duration_ms;
    config->rex2_relay_duration_ms = stored.rex2_relay_duration_ms;
    config->reader1_relay_gpio = stored.reader1_relay_gpio;
    config->reader2_relay_gpio = stored.reader2_relay_gpio;
    config->reader1_relay_duration_ms = stored.reader1_relay_duration_ms;
    config->reader2_relay_duration_ms = stored.reader2_relay_duration_ms;
    config->input_debounce_ms = stored.input_debounce_ms;
    clamp_config(config);

    ESP_LOGI(TAG, "Loaded REX config: rex1_relay=%u rex1_ms=%u rex2_relay=%u rex2_ms=%u",
             config->rex1_relay_gpio,
             config->rex1_relay_duration_ms,
             config->rex2_relay_gpio,
             config->rex2_relay_duration_ms);


    ESP_LOGI(TAG, "Loaded READER config: reader1_relay=%u reader1_ms=%u reader2_relay=%u reader2_ms=%u",
             config->reader1_relay_gpio,
             config->reader1_relay_duration_ms,
             config->reader2_relay_gpio,
             config->reader2_relay_duration_ms);
    return ESP_OK;
}
