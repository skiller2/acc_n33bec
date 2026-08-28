#include "config.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include <esp_mac.h>
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config";
static const char *NVS_NAMESPACE = "device_config";
static const char *NVS_KEY = "config";
static const uint32_t CONFIG_MAGIC = 0x434F4E46; // 'CONF'
static const uint8_t CONFIG_VERSION = 2;


static void set_defaults(config_t *config)
{
    config->rex1_relay_number = 1;
    config->rex2_relay_number = 2;
    config->port1_relay_number = 1;
    config->port1_relay2_number = 2;
    config->port2_relay_number = 3;
    config->port2_relay2_number = 2;
    config->rex1_relay_duration_ms = 2000;
    config->rex2_relay_duration_ms = 2000;
    config->port1_relay_duration_ms = 2000;
    config->port1_relay2_duration_ms = 2000;
    config->port2_relay_duration_ms = 2000;
    config->port2_relay2_duration_ms = 2000;
    config->input_debounce_ms = 100;
    config->device_id = 0; // Default device ID, will be set to last byte of MAC if not specified
    config->url_n33bec[0] = '\0'; // Default to empty string
    config->cod_tema[0] = '\0'; // Default to empty string
    config->keep_alive_secs = 30; // Default keep alive interval to 30 seconds
}

static bool valid_relay_number(uint8_t relay)
{
    return relay <= 3;
}

static void clamp_config(config_t *config)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_ETH);

    if (!valid_relay_number(config->rex1_relay_number)) {
        config->rex1_relay_number = 1;
    }
    if (!valid_relay_number(config->rex2_relay_number)) {
        config->rex2_relay_number  = 2;
    }
    if (!valid_relay_number(config->port1_relay_number)) {
        config->port1_relay_number = 1;
    }
    if (!valid_relay_number(config->port1_relay2_number)) {
        config->port1_relay2_number = 2;
    }
    if (!valid_relay_number(config->port2_relay_number)) {
        config->port2_relay_number = 3;
    }
    if (!valid_relay_number(config->port2_relay2_number)) {
        config->port2_relay2_number = 2;
    }
    if (config->rex1_relay_duration_ms == 0) {
        config->rex1_relay_duration_ms = 2000;
    }
    if (config->rex2_relay_duration_ms == 0) {
        config->rex2_relay_duration_ms = 2000;
    }
    if (config->port1_relay_duration_ms == 0) {
        config->port1_relay_duration_ms = 2000;
    }
    if (config->port1_relay2_duration_ms == 0) {
        config->port1_relay2_duration_ms = 2000;
    }
    if (config->port2_relay_duration_ms == 0) {
        config->port2_relay_duration_ms = 2000;
    }
    if (config->port2_relay2_duration_ms == 0) {
        config->port2_relay2_duration_ms = 2000;
    }
    if (config->input_debounce_ms == 0) {
        config->input_debounce_ms = 100;
    }
    if (config->keep_alive_secs == 0) {
        config->keep_alive_secs = 10; // Use the last byte of the MAC address as the device ID    
    }

    if (config->device_id == 0) {
        config->device_id = mac[4]*256+ mac[5]; // Use the last byte of the MAC address as the device ID    
    }
    if (strlen(config->url_n33bec) == 0) {
        strncpy(config->url_n33bec, "https://pepaofi.efaisa.com.ar/api/v1/movieventos/evento", sizeof(config->url_n33bec) - 1);
        config->url_n33bec[sizeof(config->url_n33bec) - 1] = '\0'; // Ensure null termination
    }
    if (strlen(config->cod_tema) == 0) {
        strncpy(config->cod_tema, "demo/acceso", sizeof(config->cod_tema) - 1);
        config->cod_tema[sizeof(config->cod_tema) - 1] = '\0'; // Ensure null termination
    }
}

esp_err_t config_save(const config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    config_t stored = {
        .magic = CONFIG_MAGIC,
        .version = CONFIG_VERSION,
        .rex1_relay_number = config->rex1_relay_number,
        .rex2_relay_number = config->rex2_relay_number,
        .rex1_relay_duration_ms = config->rex1_relay_duration_ms,
        .rex2_relay_duration_ms = config->rex2_relay_duration_ms,
        .port1_relay_number = config->port1_relay_number,
        .port1_relay2_number = config->port1_relay2_number,
        .port2_relay_number = config->port2_relay_number,
        .port2_relay2_number = config->port2_relay2_number,
        .port1_relay_duration_ms = config->port1_relay_duration_ms,
        .port1_relay2_duration_ms = config->port1_relay2_duration_ms,
        .port2_relay_duration_ms = config->port2_relay_duration_ms,
        .port2_relay2_duration_ms = config->port2_relay2_duration_ms,
        .input_debounce_ms = config->input_debounce_ms,
        .device_id = config->device_id,
        .keep_alive_secs = config->keep_alive_secs
    };
    strcpy(stored.url_n33bec, config->url_n33bec);
    strcpy(stored.cod_tema, config->cod_tema);

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace for config: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs, NVS_KEY, &stored, sizeof(stored));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit config to NVS: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Saved Port config: port1_relay=%u port1_ms=%u port2_relay=%u port2_ms=%u",
             stored.port1_relay_number,
             stored.port1_relay_duration_ms,
             stored.port2_relay_number,
             stored.port2_relay_duration_ms);

    ESP_LOGI(TAG, "Saved REX config: rex1_relay=%u rex1_ms=%u rex2_relay=%u rex2_ms=%u",
             stored.rex1_relay_number,
             stored.rex1_relay_duration_ms,
             stored.rex2_relay_number,
             stored.rex2_relay_duration_ms);

    config_load(&g_config);

    return ESP_OK;
}

esp_err_t config_load(config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    set_defaults(config);

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Config not found in NVS, using defaults");
        return config_save(config);
    }

    config_t stored;
    size_t len = sizeof(stored);
    err = nvs_get_blob(nvs, NVS_KEY, &stored, &len);
    nvs_close(nvs);

    if (err != ESP_OK || len != sizeof(stored) || stored.magic != CONFIG_MAGIC || stored.version != CONFIG_VERSION) {
        ESP_LOGW(TAG, "Invalid config in NVS or unsupported version, using defaults");
        return config_save(config);
    }

    config->rex1_relay_number = stored.rex1_relay_number;
    config->rex2_relay_number = stored.rex2_relay_number;
    config->rex1_relay_duration_ms = stored.rex1_relay_duration_ms;
    config->rex2_relay_duration_ms = stored.rex2_relay_duration_ms;
    config->port1_relay_number = stored.port1_relay_number;
    config->port1_relay2_number = stored.port1_relay2_number;
    config->port2_relay_number = stored.port2_relay_number;
    config->port2_relay2_number = stored.port2_relay2_number;
    config->port1_relay_duration_ms = stored.port1_relay_duration_ms;
    config->port1_relay2_duration_ms = stored.port1_relay2_duration_ms;
    config->port2_relay_duration_ms = stored.port2_relay_duration_ms;
    config->port2_relay2_duration_ms = stored.port2_relay2_duration_ms;
    config->input_debounce_ms = stored.input_debounce_ms;
    config->device_id = stored.device_id;
    config->keep_alive_secs = stored.keep_alive_secs;
    strncpy(config->url_n33bec, stored.url_n33bec, sizeof(config->url_n33bec) - 1);
    config->url_n33bec[sizeof(config->url_n33bec) - 1] = '\0'; // Ensure null termination
    
    strncpy(config->cod_tema, stored.cod_tema, sizeof(config->cod_tema) - 1);
    config->cod_tema[sizeof(config->cod_tema) - 1] = '\0'; // Ensure null termination
    
    clamp_config(config);


    return ESP_OK;
}

static const char *BARRIER_NVS_NAMESPACE = "barrier_config";
static const char *BARRIER_NVS_KEY = "config";
static const uint32_t BARRIER_CONFIG_MAGIC = 0x4241524E; // 'BARN'
static const uint8_t BARRIER_CONFIG_VERSION = 2;

static void barrier_set_defaults(barrier_config_t *cfg)
{
    cfg->barrier_opening_ms = 3000;
    cfg->barrier_closing_ms = 3000;
    cfg->barrier_open_ms = 5000;
    cfg->loop_active_high = 0;
}

esp_err_t barrier_config_load(barrier_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    barrier_set_defaults(cfg);

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(BARRIER_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Barrier config not found in NVS, using defaults");
        return barrier_config_save(cfg);
    }

    barrier_config_t stored;
    size_t len = sizeof(stored);
    err = nvs_get_blob(nvs, BARRIER_NVS_KEY, &stored, &len);
    nvs_close(nvs);

    if (err != ESP_OK || len != sizeof(stored) || stored.magic != BARRIER_CONFIG_MAGIC || stored.version != BARRIER_CONFIG_VERSION) {
        ESP_LOGW(TAG, "Invalid barrier config in NVS, using defaults");
        return barrier_config_save(cfg);
    }

    cfg->barrier_opening_ms = stored.barrier_opening_ms;
    cfg->barrier_closing_ms = stored.barrier_closing_ms;
    cfg->barrier_open_ms = stored.barrier_open_ms;
    cfg->loop_active_high = stored.loop_active_high;

    return ESP_OK;
}

esp_err_t barrier_config_save(const barrier_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    barrier_config_t stored = {
        .magic = BARRIER_CONFIG_MAGIC,
        .version = BARRIER_CONFIG_VERSION,
        .barrier_opening_ms = cfg->barrier_opening_ms,
        .barrier_closing_ms = cfg->barrier_closing_ms,
        .barrier_open_ms = cfg->barrier_open_ms,
        .loop_active_high = cfg->loop_active_high
    };

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(BARRIER_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace for barrier config: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs, BARRIER_NVS_KEY, &stored, sizeof(stored));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save barrier config to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit barrier config to NVS: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Saved barrier config: opening=%u closing=%u open=%u",
             stored.barrier_opening_ms, stored.barrier_closing_ms, stored.barrier_open_ms);

    return ESP_OK;
}

barrier_config_t g_barrier_config;
