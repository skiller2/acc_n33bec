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
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"

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

static int s_disconnect_retry_count = 0;

static wifi_status_t s_wifi_status = WIFI_STATUS_DISCONNECTED;
static char s_dpp_uri[MAX_DPP_URI_LEN] = {0};
static char s_connected_ssid[MAX_SSID_LEN + 1] = {0};
static char s_ip_str[MAX_IP_LEN] = {0};
static SemaphoreHandle_t s_wifi_mutex = NULL;
static bool s_dpp_initialized = false;
static bool s_wifi_driver_started = false;
static bool s_wifi_stopped_by_ethernet = false;

#define WIFI_NVS_NAMESPACE "wifi_creds"
#define WIFI_NVS_KEY_SSID  "ssid"
#define WIFI_NVS_KEY_PASS  "password"

esp_err_t wifi_get_credentials(char *ssid_buf, size_t ssid_buf_len,
                               char *pass_buf, size_t pass_buf_len)
{
    if (!ssid_buf || !pass_buf || ssid_buf_len == 0 || pass_buf_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ssid_buf[0] = '\0';
    pass_buf[0] = '\0';

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t s_len = ssid_buf_len;
    size_t p_len = pass_buf_len;
    esp_err_t err_s = nvs_get_str(nvs, WIFI_NVS_KEY_SSID, ssid_buf, &s_len);
    esp_err_t err_p = nvs_get_str(nvs, WIFI_NVS_KEY_PASS, pass_buf, &p_len);
    nvs_close(nvs);

    if (err_s != ESP_OK || err_p != ESP_OK || ssid_buf[0] == '\0') {
        ssid_buf[0] = '\0';
        pass_buf[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t wifi_clear_credentials(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(nvs, WIFI_NVS_KEY_SSID);
    nvs_erase_key(nvs, WIFI_NVS_KEY_PASS);
    err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static esp_err_t wifi_save_credentials(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for wifi creds: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(nvs, WIFI_NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, WIFI_NVS_KEY_PASS, password ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save wifi creds: %s", esp_err_to_name(err));
    }
    return err;
}

char *wifi_scan_to_json(void)
{
    if (!s_dpp_initialized) {
        ESP_LOGW(TAG, "Cannot scan: WiFi not initialised");
        return NULL;
    }

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        return NULL;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan get_ap_num failed: %s", esp_err_to_name(err));
        return NULL;
    }

    wifi_ap_record_t *ap_list = NULL;
    if (ap_count > 0) {
        ap_list = calloc(ap_count, sizeof(wifi_ap_record_t));
        if (!ap_list) {
            return NULL;
        }
        err = esp_wifi_scan_get_ap_records(&ap_count, ap_list);
        if (err != ESP_OK) {
            free(ap_list);
            return NULL;
        }
    }

    cJSON *root = cJSON_CreateArray();
    if (!root) {
        free(ap_list);
        return NULL;
    }

    for (uint16_t i = 0; i < ap_count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddStringToObject(item, "ssid", (const char *)ap_list[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", ap_list[i].rssi);
        cJSON_AddNumberToObject(item, "channel", ap_list[i].primary);
        cJSON_AddNumberToObject(item, "authmode", ap_list[i].authmode);
        cJSON_AddItemToArray(root, item);
    }

    free(ap_list);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

esp_err_t wifi_connect_sta(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ssid) > MAX_SSID_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password && strlen(password) > 63) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wifi_save_credentials(ssid, password);
    if (err != ESP_OK) {
        return err;
    }

    s_disconnect_retry_count = 0;

    if (s_wifi_stopped_by_ethernet && !s_wifi_driver_started) {
        ESP_LOGW(TAG, "WiFi disabled by ethernet, cannot connect");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_dpp_initialized) {
        if (s_wifi_driver_started) {
            /* WiFi was previously initialised and then stopped. The driver
             * state is gone; we cannot safely call wifi_init() again because
             * the default STA netif still exists. Refuse the request. */
            ESP_LOGE(TAG, "WiFi stopped after init; reboot required to reconnect");
            return ESP_ERR_INVALID_STATE;
        }
        err = wifi_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_wifi_status = WIFI_STATUS_CONNECTING;
        memset(s_connected_ssid, 0, sizeof(s_connected_ssid));
        strncpy(s_connected_ssid, ssid, sizeof(s_connected_ssid) - 1);
        xSemaphoreGive(s_wifi_mutex);
    }

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    if (password) {
        strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1);
    }
    sta_cfg.sta.threshold.authmode = (password && password[0] != '\0')
                                        ? WIFI_AUTH_WPA2_PSK
                                        : WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(err));
        return err;
    }

    /* Stop DPP listening so it does not block the STA connection with
     * "Do not go offchannel when sta is connecting". We also deinit the DPP
     * supplicant entirely to fully release the offchannel ROC. The user can
     * re-enable DPP via /dpp/bootstrap after clearing credentials. */
    if (s_dpp_initialized) {
        esp_err_t stop_err = esp_supp_dpp_stop_listen();
        if (stop_err != ESP_OK) {
            ESP_LOGW(TAG, "esp_supp_dpp_stop_listen: %s", esp_err_to_name(stop_err));
        }
        esp_err_t deinit_err = esp_supp_dpp_deinit();
        if (deinit_err != ESP_OK) {
            ESP_LOGW(TAG, "esp_supp_dpp_deinit: %s", esp_err_to_name(deinit_err));
        }
        s_dpp_initialized = false;
    }

    wifi_broadcast_state();

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Connecting to SSID '%s'...", ssid);
    return ESP_OK;
}

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
            /* Start DPP enrollee listening only if no STA credentials are
             * configured. When credentials are present the user wants the
             * device to behave as a plain station and DPP must NOT touch the
             * channel. */
            {
                char chk_ssid[4] = {0};
                char chk_pass[4] = {0};
                bool has_creds = (wifi_get_credentials(chk_ssid, sizeof(chk_ssid),
                                                     chk_pass, sizeof(chk_pass)) == ESP_OK);
                if (has_creds) {
                    ESP_LOGI(TAG, "Stored STA credentials present, DPP disabled for this session");
                    if (s_dpp_initialized) {
                        esp_supp_dpp_stop_listen();
                        esp_supp_dpp_deinit();
                        s_dpp_initialized = false;
                    }
                } else {
                    if (esp_supp_dpp_start_listen() != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to start DPP listen");
                    } else {
                        ESP_LOGI(TAG, "DPP enrollee listening for authentication");
                    }
                }
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "WiFi station disconnected (reason=%d)", disc ? disc->reason : -1);
            if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                s_wifi_status = WIFI_STATUS_DISCONNECTED;
                memset(s_ip_str, 0, sizeof(s_ip_str));
                xSemaphoreGive(s_wifi_mutex);
            }
            wifi_broadcast_state();
            /* Retry connection once after a short delay if a SSID is configured.
             * After that we stop retrying to avoid tight reconnect loops when
             * the credentials are wrong. The user can retry manually from the
             * web UI. */
            if (s_connected_ssid[0] != '\0' && s_disconnect_retry_count < 1) {
                s_disconnect_retry_count++;
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_wifi_connect();
            }
            break;
        }

        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *evt = (wifi_event_sta_connected_t *)event_data;
            s_disconnect_retry_count = 0;
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
            /* Only restart DPP listen if STA credentials are not configured */
            char chk_ssid[4] = {0};
            char chk_pass[4] = {0};
            bool has_creds = (wifi_get_credentials(chk_ssid, sizeof(chk_ssid),
                                                  chk_pass, sizeof(chk_pass)) == ESP_OK);
            if (!has_creds && esp_supp_dpp_start_listen() != ESP_OK) {
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

    if (s_wifi_stopped_by_ethernet) {
        ESP_LOGI(TAG, "Ethernet already has IP, skipping WiFi init");
        return ESP_OK;
    }

    s_wifi_mutex = xSemaphoreCreateMutex();
    if (s_wifi_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Create default WiFi STA netif */
    esp_netif_create_default_wifi_sta();
    s_wifi_driver_started = true;

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

    /* If there are stored STA credentials, apply them so the device
     * auto-connects on boot. The driver holds the config until we ask it to
     * connect, which we do after WiFi is up. */
    char stored_ssid[33] = {0};
    char stored_pass[64] = {0};
    if (wifi_get_credentials(stored_ssid, sizeof(stored_ssid), stored_pass, sizeof(stored_pass)) == ESP_OK) {
        wifi_config_t sta_cfg = {0};
        strncpy((char *)sta_cfg.sta.ssid, stored_ssid, sizeof(sta_cfg.sta.ssid) - 1);
        strncpy((char *)sta_cfg.sta.password, stored_pass, sizeof(sta_cfg.sta.password) - 1);
        sta_cfg.sta.threshold.authmode = (stored_pass[0] != '\0')
                                            ? WIFI_AUTH_WPA2_PSK
                                            : WIFI_AUTH_OPEN;
        if (esp_wifi_set_config(WIFI_IF_STA, &sta_cfg) == ESP_OK) {
            esp_supp_dpp_stop_listen();
            esp_supp_dpp_deinit();
            s_dpp_initialized = false;
            esp_wifi_connect();
            if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                memset(s_connected_ssid, 0, sizeof(s_connected_ssid));
                strncpy(s_connected_ssid, stored_ssid, sizeof(s_connected_ssid) - 1);
                xSemaphoreGive(s_wifi_mutex);
            }
        }
    }

    if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_wifi_status = WIFI_STATUS_DPP_LISTENING;
        xSemaphoreGive(s_wifi_mutex);
    }

    wifi_broadcast_state();

    ESP_LOGI(TAG, "WiFi with DPP enrollee initialized, listening on channel(s): %s", DPP_LISTEN_CHANNEL_LIST);
    return ESP_OK;
}

void wifi_stop(void)
{
    if (!s_dpp_initialized) {
        return;
    }

    esp_supp_dpp_deinit();
    s_dpp_initialized = false;

    if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_wifi_status = WIFI_STATUS_DISCONNECTED;
        s_connected_ssid[0] = 0;
        s_ip_str[0] = 0;
        xSemaphoreGive(s_wifi_mutex);
    }

    esp_wifi_stop();
    esp_wifi_deinit();
    s_wifi_driver_started = false;

    ESP_LOGI(TAG, "WiFi stopped");
}

void wifi_request_stop_by_ethernet(void)
{
    s_wifi_stopped_by_ethernet = true;
    if (s_dpp_initialized) {
        wifi_stop();
    }
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

esp_err_t wifi_start_service_mode(void)
{
    if (s_dpp_initialized) {
        esp_supp_dpp_deinit();
        s_dpp_initialized = false;
    }

    if (!s_wifi_mutex) {
        s_wifi_mutex = xSemaphoreCreateMutex();
        if (!s_wifi_mutex) {
            ESP_LOGE(TAG, "Failed to create WiFi mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    esp_netif_t *ap_netif = (esp_netif_t *)esp_netif_create_default_wifi_ap();

    char hostname[32] = "esp_ap";
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif) {
        const char *h = NULL;
        if (esp_netif_get_hostname(netif, &h) == ESP_OK && h && h[0] != '\0') {
            strncpy(hostname, h, sizeof(hostname) - 1);
            hostname[sizeof(hostname) - 1] = '\0';
        }
    }
    if (ap_netif && hostname[0] != '\0') {
        esp_netif_set_hostname(ap_netif, hostname);
    }

    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, hostname, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = (uint8_t)strlen(hostname);
    strncpy((char *)ap_config.ap.password, "12345678", sizeof(ap_config.ap.password) - 1);
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.channel = 1;

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_wifi_status = WIFI_STATUS_AP_ACTIVE;
        xSemaphoreGive(s_wifi_mutex);
    }

    ESP_LOGI(TAG, "AP started: SSID='%s' password='12345678'", hostname);
    return ESP_OK;
}
