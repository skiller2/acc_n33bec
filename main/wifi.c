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
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"

#ifdef CONFIG_ESP_DPP_ENABLE_QRCODE
#include "qrcode.h"
#endif

#include "connect.h"
#include "wifi.h"
#include "ws.h"

/* -------------------------------------------------------------------------- */
/* Configuration                                                               */
/* -------------------------------------------------------------------------- */

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

#define CURVE_SEC256R1_PKEY_HEX_DIGITS 64
#define MAX_DPP_URI_LEN 512
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64
#define MAX_IP_LEN 16

#define WIFI_NVS_NAMESPACE "wifi_creds"
#define WIFI_NVS_KEY_SSID "ssid"
#define WIFI_NVS_KEY_PASS "password"

#define WIFI_DISCONNECT_RETRY_MAX 1
#define WIFI_DISCONNECT_RETRY_DELAY_MS 2000

/* -------------------------------------------------------------------------- */
/* Module state                                                                */
/* -------------------------------------------------------------------------- */

static const char *TAG = "wifi";

static char s_dpp_uri[MAX_DPP_URI_LEN];
static char ssid[MAX_SSID_LEN + 1] = {0};
static char s_ip_str[MAX_IP_LEN];
static int s_disconnect_retry_count = 0;
static char s_status[64] = {0};

/* -------------------------------------------------------------------------- */
/* Forward declarations                                                        */
/* -------------------------------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void dpp_bootstrap_generate(void);
static void print_qr_code(const char *uri);

/* -------------------------------------------------------------------------- */
/* NVS helpers                                                                 */
/* -------------------------------------------------------------------------- */

esp_err_t wifi_save_credentials(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS for wifi creds: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs, WIFI_NVS_KEY_SSID, ssid);
    if (err == ESP_OK)
    {
        err = nvs_set_str(nvs, WIFI_NVS_KEY_PASS, password ? password : "");
    }
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save wifi creds: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t wifi_get_credentials(char *ssid_buf, size_t ssid_buf_len,
                               char *pass_buf, size_t pass_buf_len)
{
    if (!ssid_buf || !pass_buf || ssid_buf_len == 0 || pass_buf_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    ssid_buf[0] = '\0';
    pass_buf[0] = '\0';

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        return ESP_ERR_NOT_FOUND;
    }

    size_t s_len = ssid_buf_len;
    size_t p_len = pass_buf_len;
    esp_err_t err_s = nvs_get_str(nvs, WIFI_NVS_KEY_SSID, ssid_buf, &s_len);
    esp_err_t err_p = nvs_get_str(nvs, WIFI_NVS_KEY_PASS, pass_buf, &p_len);
    nvs_close(nvs);

    if (err_s != ESP_OK || err_p != ESP_OK || ssid_buf[0] == '\0')
    {
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
    if (err != ESP_OK)
    {
        return err;
    }
    nvs_erase_key(nvs, WIFI_NVS_KEY_SSID);
    nvs_erase_key(nvs, WIFI_NVS_KEY_PASS);
    err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

char *wifi_scan_to_json(void)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK)
    {
        ESP_LOGE(TAG, "Scan start failed");
        return NULL;
    }

    uint16_t ap_count = 0;
    if (esp_wifi_scan_get_ap_num(&ap_count) != ESP_OK)
    {
        ESP_LOGE(TAG, "Scan get_ap_num failed");
        return NULL;
    }

    wifi_ap_record_t *ap_list = NULL;
    if (ap_count > 0)
    {
        ap_list = calloc(ap_count, sizeof(wifi_ap_record_t));
        if (!ap_list)
        {
            return NULL;
        }
        if (esp_wifi_scan_get_ap_records(&ap_count, ap_list) != ESP_OK)
        {
            free(ap_list);
            return NULL;
        }
    }

    cJSON *root = cJSON_CreateArray();
    if (!root)
    {
        free(ap_list);
        return NULL;
    }

    for (uint16_t i = 0; i < ap_count; ++i)
    {
        cJSON *item = cJSON_CreateObject();
        if (!item)
            continue;
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

/* -------------------------------------------------------------------------- */
/* DPP helpers                                                                 */
/* -------------------------------------------------------------------------- */

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
    size_t key_len = strlen(DPP_BOOTSTRAPPING_KEY ? DPP_BOOTSTRAPPING_KEY : "");
    char *key = NULL;

    if (key_len)
    {
        if (key_len != CURVE_SEC256R1_PKEY_HEX_DIGITS)
        {
            ESP_LOGW(TAG, "Invalid key length! Private key needs to be 32 bytes (or 64 hex digits) long");
        }
        else
        {
            char prefix[] = "30310201010420";
            char postfix[] = "a00a06082a8648ce3d030107";
            key = malloc(strlen(prefix) + key_len + strlen(postfix) + 1);
            if (key)
            {
                snprintf(key, strlen(prefix) + key_len + strlen(postfix) + 1,
                         "%s%s%s", prefix, DPP_BOOTSTRAPPING_KEY, postfix);
            }
        }
    }

    esp_err_t ret = esp_supp_dpp_bootstrap_gen(DPP_LISTEN_CHANNEL_LIST, DPP_BOOTSTRAP_QR_CODE,
                                               key, DPP_DEVICE_INFO);
    if (key)
    {
        free(key);
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "DPP bootstrap generation failed: %s", esp_err_to_name(ret));
    }
}

/* -------------------------------------------------------------------------- */
/* WiFi event handler                                                          */
/* -------------------------------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGW(TAG, "EVENT (%d) (%d)", event_base, event_id);
 
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "WiFi station disconnected (reason=%d)", disc ? disc->reason : -1);
            memset(s_ip_str, 0, sizeof(s_ip_str));
            strcpy(s_status, "WIFI_EVENT_STA_DISCONNECTED");
            ws_broadcast_wifi_status(s_status, ssid, s_ip_str, s_dpp_uri);


            /* Single retry after a short delay to avoid tight reconnect loops
             * when the credentials are wrong. The user can retry from the UI. */
            if (ssid[0] != '\0' &&
                s_disconnect_retry_count < WIFI_DISCONNECT_RETRY_MAX)
            {
                s_disconnect_retry_count++;
                vTaskDelay(pdMS_TO_TICKS(WIFI_DISCONNECT_RETRY_DELAY_MS));
                esp_wifi_connect();
            }
            break;
        }

        case WIFI_EVENT_STA_CONNECTED:
        {
            wifi_event_sta_connected_t *evt = (wifi_event_sta_connected_t *)event_data;
            s_disconnect_retry_count = 0;
            if (evt && evt->ssid_len > 0)
            {
                ESP_LOGI(TAG, "Connected to AP: %.*s", evt->ssid_len, (const char *)evt->ssid);

                memset(ssid, 0, sizeof(ssid));
                memcpy(ssid, evt->ssid, evt->ssid_len);

                strcpy(s_status, "WIFI_EVENT_STA_CONNECTED");
                ws_broadcast_wifi_status(s_status, ssid, s_ip_str, s_dpp_uri);


            }
            break;
        }

        case WIFI_EVENT_DPP_URI_READY:
        {
            wifi_event_dpp_uri_ready_t *uri_data = (wifi_event_dpp_uri_ready_t *)event_data;
            if (uri_data && uri_data->uri_data_len > 0)
            {
                ESP_LOGI(TAG, "DPP URI ready (len=%u)", uri_data->uri_data_len);
                memset(s_dpp_uri, 0, sizeof(s_dpp_uri));
                strncpy(s_dpp_uri, uri_data->uri, sizeof(s_dpp_uri) - 1);

                strcpy(s_status, "WIFI_EVENT_DPP_URI_READY");
                ws_broadcast_wifi_status(s_status, ssid, s_ip_str, s_dpp_uri);

                print_qr_code(uri_data->uri);
            }
            break;
        }

        case WIFI_EVENT_DPP_CFG_RECVD:
        {
            wifi_event_dpp_config_received_t *cfg = (wifi_event_dpp_config_received_t *)event_data;
            ESP_LOGI(TAG, "DPP config received, connecting to SSID: %s",
                     (const char *)cfg->wifi_cfg.sta.ssid);
            memset(ssid, 0, sizeof(ssid));
            strncpy(ssid, (const char *)cfg->wifi_cfg.sta.ssid, sizeof(ssid) - 1);
            wifi_save_credentials((const char *)cfg->wifi_cfg.sta.ssid,
                                  (const char *)cfg->wifi_cfg.sta.password);
            wifi_sta_start();
            break;
        }

        case WIFI_EVENT_DPP_FAILED:
        {
            wifi_event_dpp_failed_t *dpp_fail = (wifi_event_dpp_failed_t *)event_data;
            ESP_LOGW(TAG, "DPP authentication failed (reason: %s)",
                     esp_err_to_name((int)dpp_fail->failure_reason));
            break;
        }

        default:
            break;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        /* Promote WiFi STA to the default netif when Ethernet has no link.
         * This is what user-facing sockets (HTTP, SNTP, DNS) use. */
        if (event->esp_netif)
        {
            esp_netif_t *eth_netif = esp_netif_get_default_netif();
            if (eth_netif && esp_netif_get_desc(eth_netif) &&
                strncmp(esp_netif_get_desc(eth_netif), "eth", 3) == 0)
            {
                /* keep ethernet default if ethernet has IP */
                esp_netif_ip_info_t eth_ip;
                if (esp_netif_get_ip_info(eth_netif, &eth_ip) != ESP_OK ||
                    eth_ip.ip.addr == 0)
                {
                    esp_netif_set_default_netif(event->esp_netif);
                    ESP_LOGI(TAG, "Set WiFi STA as default netif (ethernet has no IP)");
                }
            }
            else
            {
                esp_netif_set_default_netif(event->esp_netif);
            }
        }
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_ip_event_group, HAVE_IP);

        strcpy(s_status, "WIFI_EVENT_STA_GOT_IP");
        ws_broadcast_wifi_status(s_status, ssid, s_ip_str, s_dpp_uri);
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

esp_err_t wifi_sta_start(void)
{
    char stored_pass[MAX_PASS_LEN] = {0};
    esp_err_t err;
    wifi_config_t sta_cfg = {0};

    err = wifi_get_credentials(ssid, sizeof(ssid),
                               stored_pass, sizeof(stored_pass));

    ESP_LOGE(TAG, "wifi_sta_start SSID: %s, password: %s", ssid, stored_pass);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "wifi_get_credentials failed: %s", esp_err_to_name(err));
        return err;
    }

    
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, stored_pass, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = (stored_pass[0] != '\0')
                                         ? WIFI_AUTH_WPA2_PSK
                                         : WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;
    
    err= esp_wifi_set_config(WIFI_IF_STA, &sta_cfg); 
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return err;
    }
    esp_wifi_start();
    
    
    err = esp_wifi_connect();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Connecting to SSID '%s'...", ssid);
    return ESP_OK;
}

esp_err_t dpp_start(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    esp_err_t ret = esp_supp_dpp_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init DPP: %s", esp_err_to_name(ret));
        return ret;
    }

    dpp_bootstrap_generate();
    ret = esp_wifi_start();

    ESP_LOGI(TAG, "DPP listening on channel(s): %s", DPP_LISTEN_CHANNEL_LIST);
    return ret;
}

esp_err_t wifi_init(void)
{
    char hostname[32] = "esp_ap";
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    esp_netif_t *netif = esp_netif_get_default_netif();
    esp_netif_t *netifsta = esp_netif_create_default_wifi_sta();
    esp_netif_t *netifap = esp_netif_create_default_wifi_ap();

    //set_default_wifi_handlers();  //For STA and AP events

    if (netif)
    {
        const char *h = NULL;
        if (esp_netif_get_hostname(netif, &h) == ESP_OK && h && h[0] != '\0')
        {
            strncpy(hostname, h, sizeof(hostname) - 1);
            hostname[sizeof(hostname) - 1] = '\0';
        }
    }

    esp_netif_set_hostname(netifsta, hostname);
    esp_netif_set_hostname(netifap, hostname);

    return ESP_OK;
}

void wifi_stop(void)
{
    esp_supp_dpp_stop_listen();
    esp_supp_dpp_deinit();
    esp_wifi_stop();

    // esp_wifi_deinit();

    ESP_LOGI(TAG, "WiFi stopped");
    strcpy(s_status, "WIFI_STOP");
    ws_broadcast_wifi_status(s_status, ssid, s_ip_str, s_dpp_uri);
}

esp_err_t dpp_trigger_bootstrap(void)
{

    ESP_LOGI(TAG, "Triggering DPP bootstrap regeneration");

    esp_err_t deinit_err = esp_supp_dpp_deinit();
    if (deinit_err != ESP_OK)
    {
        ESP_LOGW(TAG, "DPP deinit returned %s", esp_err_to_name(deinit_err));
    }

    ESP_ERROR_CHECK(esp_supp_dpp_init());

    dpp_bootstrap_generate();

    esp_err_t err = esp_supp_dpp_start_listen();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to restart DPP listen: %s", esp_err_to_name(err));
        return err;
    }

    strcpy(s_status, "WIFI_EVENT_DPP_TRIGGERED");
    ws_broadcast_wifi_status(s_status, ssid, s_ip_str, s_dpp_uri);
    return ESP_OK;
}

esp_err_t wifi_get_dpp_uri(char *buf, size_t len)
{
    if (!buf || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dpp_uri[0] == '\0')
    {
        return ESP_ERR_NOT_FOUND;
    }
    strncpy(buf, s_dpp_uri, len - 1);
    buf[len - 1] = '\0';
    return ESP_OK;
    return ESP_ERR_TIMEOUT;
}

/* -------------------------------------------------------------------------- */
/* Service-mode AP                                                             */
/* -------------------------------------------------------------------------- */

esp_err_t wifi_start_service_mode(void)
{
    wifi_config_t ap_config = {0};
    char hostname[32] = "esp_ap";

    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif)
    {
        const char *h = NULL;
        if (esp_netif_get_hostname(netif, &h) == ESP_OK && h && h[0] != '\0')
        {
            strncpy(hostname, h, sizeof(hostname) - 1);
            hostname[sizeof(hostname) - 1] = '\0';
        }
    }

    strncpy((char *)ap_config.ap.ssid, hostname, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = (uint8_t)strlen(hostname);
    strncpy((char *)ap_config.ap.password, "12345678", sizeof(ap_config.ap.password) - 1);
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.channel = 1;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: SSID='%s' password='12345678'", hostname);
    return ESP_OK;
}

void ws_broadcast_wifi_status_last(void)
{
    ws_broadcast_wifi_status(s_status, ssid, s_ip_str, s_dpp_uri);
}
