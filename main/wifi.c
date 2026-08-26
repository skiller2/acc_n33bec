/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_dpp.h"
#include "esp_wifi_types.h"
#include "connect.h"

#ifdef CONFIG_ESP_DPP_ENABLE_QRCODE
#include "qrcode.h"
#endif

#include "wifi.h"
#include "ws.h"

#ifdef CONFIG_ESP_DPP_LISTEN_CHANNEL_LIST
#define DPP_LISTEN_CHANNEL_LIST CONFIG_ESP_DPP_LISTEN_CHANNEL_LIST
#else
#define DPP_LISTEN_CHANNEL_LIST "6"
#endif

#ifdef CONFIG_ESP_DPP_BOOTSTRAPPING_KEY
#define DPP_BOOTSTRAPPING_KEY CONFIG_ESP_DPP_BOOTSTRAPPING_KEY
#else
#define DPP_BOOTSTRAPPING_KEY ""
#endif

#ifdef CONFIG_ESP_DPP_DEVICE_INFO
#define DPP_DEVICE_INFO CONFIG_ESP_DPP_DEVICE_INFO
#else
#define DPP_DEVICE_INFO NULL
#endif

#define MAX_DPP_URI_LEN  512
#define MAX_SSID_LEN     32
#define MAX_IP_LEN       16

static const char *TAG = "wifi_dpp";

static wifi_status_t s_wifi_status = WIFI_STATUS_DISCONNECTED;
static char s_dpp_uri[MAX_DPP_URI_LEN] = {0};
static char s_connected_ssid[MAX_SSID_LEN + 1] = {0};
static char s_ip_str[MAX_IP_LEN] = {0};
static SemaphoreHandle_t s_wifi_mutex = NULL;
static bool s_dpp_initialized = false;

void wifi_broadcast_state(void)
{
    wifi_status_t status;
    char ssid[MAX_SSID_LEN + 1] = {0};
    char ip[MAX_IP_LEN] = {0};
    char uri[MAX_DPP_URI_LEN] = {0};

    if (!s_wifi_mutex || xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    status = s_wifi_status;
    memcpy(ssid, s_connected_ssid, sizeof(ssid));
    memcpy(ip, s_ip_str, sizeof(ip));
    memcpy(uri, s_dpp_uri, sizeof(uri));

    xSemaphoreGive(s_wifi_mutex);

    ws_broadcast_wifi_status(status, status == WIFI_STATUS_CONNECTED, ssid, ip, uri);
}

#define CURVE_SEC256R1_PKEY_HEX_DIGITS 64

static void print_qr_code(const char *uri)
{
#ifdef CONFIG_ESP_DPP_ENABLE_QRCODE
    esp_qrcode_config_t qr_cfg = ESP_QRCODE_CONFIG_DEFAULT();
    ESP_LOGI(TAG, "Scan below QR Code to configure the enrollee:");
    esp_qrcode_generate(&qr_cfg, uri);
#else
    ESP_LOGI(TAG, "DPP QR Code URI: %s", uri);
    ESP_LOGI(TAG, "Scan this URI with a DPP configurator to provision WiFi credentials");
#endif
}

static void dpp_bootstrap_generate(void)
{
    esp_err_t ret;
    size_t key_len = strlen(DPP_BOOTSTRAPPING_KEY ? DPP_BOOTSTRAPPING_KEY : "");
    char *key = NULL;

    if (key_len) {
        if (key_len != CURVE_SEC256R1_PKEY_HEX_DIGITS) {
            ESP_LOGW(TAG, "Invalid key length! Private key needs to be 32 bytes (or 64 hex digits) long");
        } else {
            char prefix[] = "30310201010420";
            char postfix[] = "a00a06082a8648ce3d030107";
            key = malloc(strlen(prefix) + key_len + strlen(postfix) + 1);
            if (key) {
                snprintf(key, strlen(prefix) + key_len + strlen(postfix) + 1,
                         "%s%s%s", prefix, DPP_BOOTSTRAPPING_KEY, postfix);
            }
        }
    }

    ret = esp_supp_dpp_bootstrap_gen(DPP_LISTEN_CHANNEL_LIST, DPP_BOOTSTRAP_QR_CODE,
                                     key, DPP_DEVICE_INFO);
    if (key) {
        free(key);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DPP bootstrap generation failed: %s", esp_err_to_name(ret));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            /* Start DPP enrollee listening after WiFi is started */
            if (esp_supp_dpp_start_listen() != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start DPP listen");
            } else {
                ESP_LOGI(TAG, "DPP enrollee listening for authentication");
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi station disconnected");
            if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                s_wifi_status = WIFI_STATUS_DISCONNECTED;
                memset(s_ip_str, 0, sizeof(s_ip_str));
                xSemaphoreGive(s_wifi_mutex);
            }
            wifi_broadcast_state();
            /* Retry connection with stored config (driver retains last config) */
            if (s_connected_ssid[0] != '\0') {
                esp_wifi_connect();
            }
            break;

        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *evt = (wifi_event_sta_connected_t *)event_data;
            if (evt->ssid_len > 0) {
                ESP_LOGI(TAG, "Connected to AP: %.*s", evt->ssid_len, (const char *)evt->ssid);
                if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    memset(s_connected_ssid, 0, sizeof(s_connected_ssid));
                    memcpy(s_connected_ssid, evt->ssid, evt->ssid_len);
                    xSemaphoreGive(s_wifi_mutex);
                }
                wifi_broadcast_state();
            }
            break;
        }

        case WIFI_EVENT_DPP_URI_READY: {
            wifi_event_dpp_uri_ready_t *uri_data = (wifi_event_dpp_uri_ready_t *)event_data;
            if (uri_data && uri_data->uri_data_len > 0) {
                ESP_LOGI(TAG, "DPP URI ready (len=%u)", uri_data->uri_data_len);
                if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    s_wifi_status = WIFI_STATUS_DPP_READY;
                    memset(s_dpp_uri, 0, sizeof(s_dpp_uri));
                    strncpy(s_dpp_uri, uri_data->uri, sizeof(s_dpp_uri) - 1);
                    xSemaphoreGive(s_wifi_mutex);
                }
                wifi_broadcast_state();
                print_qr_code(uri_data->uri);
            }
            break;
        }

        case WIFI_EVENT_DPP_CFG_RECVD: {
            wifi_event_dpp_config_received_t *cfg = (wifi_event_dpp_config_received_t *)event_data;
            ESP_LOGI(TAG, "DPP config received, connecting to SSID: %s", (const char *)cfg->wifi_cfg.sta.ssid);
            memset(s_connected_ssid, 0, sizeof(s_connected_ssid));
            strncpy(s_connected_ssid, (const char *)cfg->wifi_cfg.sta.ssid, sizeof(s_connected_ssid) - 1);
            esp_wifi_set_config(WIFI_IF_STA, &cfg->wifi_cfg);
            if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                s_wifi_status = WIFI_STATUS_CONNECTING;
                xSemaphoreGive(s_wifi_mutex);
            }
            wifi_broadcast_state();
            esp_wifi_connect();
            break;
        }

        case WIFI_EVENT_DPP_FAILED: {
            wifi_event_dpp_failed_t *dpp_fail = (wifi_event_dpp_failed_t *)event_data;
            ESP_LOGW(TAG, "DPP authentication failed (reason: %s)",
                     esp_err_to_name((int)dpp_fail->failure_reason));
            if (esp_supp_dpp_start_listen() != ESP_OK) {
                ESP_LOGE(TAG, "Failed to restart DPP listen");
            }
            break;
        }

        default:
            break;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            s_wifi_status = WIFI_STATUS_CONNECTED;
            snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
            xSemaphoreGive(s_wifi_mutex);
        }
        xEventGroupSetBits(s_ip_event_group, HAVE_IP);
        wifi_broadcast_state();
    }
}

esp_err_t dpp_trigger_bootstrap(void)
{
    if (!s_wifi_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Triggering DPP bootstrap regeneration");

    if (!s_dpp_initialized) {
        ESP_LOGE(TAG, "DPP not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Deinit and reinit DPP to regenerate the bootstrap key */
    esp_err_t deinit_err = esp_supp_dpp_deinit();
    if (deinit_err != ESP_OK) {
        ESP_LOGW(TAG, "DPP deinit returned %s", esp_err_to_name(deinit_err));
    }

    ESP_ERROR_CHECK(esp_supp_dpp_init());
    dpp_bootstrap_generate();

    /* Restart listening (WiFi should already be started) */
    esp_err_t err = esp_supp_dpp_start_listen();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to restart DPP listen: %s", esp_err_to_name(err));
        return err;
    }

    if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_wifi_status = WIFI_STATUS_DPP_LISTENING;
        xSemaphoreGive(s_wifi_mutex);
    }

    wifi_broadcast_state();

    return ESP_OK;
}

esp_err_t wifi_init(void)
{
    esp_err_t ret;

    s_wifi_mutex = xSemaphoreCreateMutex();
    if (s_wifi_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Create default WiFi STA netif */
    esp_netif_create_default_wifi_sta();

    /* Register WiFi and IP event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    /* Initialize WiFi driver */
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* Initialize DPP supplicant */
    ret = esp_supp_dpp_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init DPP: %s", esp_err_to_name(ret));
        return ret;
    }
    s_dpp_initialized = true;

    /* Generate DPP bootstrap (QR code) */
    dpp_bootstrap_generate();

    /* Start WiFi (triggers WIFI_EVENT_STA_START which starts DPP listen) */
    ESP_ERROR_CHECK(esp_wifi_start());

    if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_wifi_status = WIFI_STATUS_DPP_LISTENING;
        xSemaphoreGive(s_wifi_mutex);
    }

    wifi_broadcast_state();

    ESP_LOGI(TAG, "WiFi with DPP enrollee initialized, listening on channel(s): %s", DPP_LISTEN_CHANNEL_LIST);
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    bool connected = false;
    if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        connected = (s_wifi_status == WIFI_STATUS_CONNECTED);
        xSemaphoreGive(s_wifi_mutex);
    }
    return connected;
}

wifi_status_t wifi_get_status(void)
{
    wifi_status_t status = WIFI_STATUS_DISCONNECTED;
    if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        status = s_wifi_status;
        xSemaphoreGive(s_wifi_mutex);
    }
    return status;
}

esp_err_t wifi_get_dpp_uri(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_wifi_mutex) {
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_dpp_uri[0] == '\0') {
            xSemaphoreGive(s_wifi_mutex);
            return ESP_ERR_NOT_FOUND;
        }
        strncpy(buf, s_dpp_uri, len - 1);
        buf[len - 1] = '\0';
        xSemaphoreGive(s_wifi_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_get_ssid(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_wifi_mutex) {
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_connected_ssid[0] == '\0') {
            xSemaphoreGive(s_wifi_mutex);
            return ESP_ERR_NOT_FOUND;
        }
        strncpy(buf, s_connected_ssid, len - 1);
        buf[len - 1] = '\0';
        xSemaphoreGive(s_wifi_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_get_ip(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_wifi_mutex) {
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_ip_str[0] == '\0') {
            xSemaphoreGive(s_wifi_mutex);
            return ESP_ERR_NOT_FOUND;
        }
        strncpy(buf, s_ip_str, len - 1);
        buf[len - 1] = '\0';
        xSemaphoreGive(s_wifi_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}
