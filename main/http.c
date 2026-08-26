#include "esp_http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_log.h"
#include "config.h"
#include "wifi.h"
#include "protocol_examples_common.h"
#include "cJSON.h"
#include "wiegand_local.h"
#include "beep.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_efuse.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "esp_app_desc.h"
#include <driver/gpio.h>
#include "ws.h"

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "dev"
#endif

extern void card_add(uint64_t);
extern void card_del(uint64_t);
// extern char *log_read_all_json(void);
extern esp_err_t log_read_all_json(httpd_req_t *req);
extern char *card_read_all_json(void);
extern void dispatch_log_event(uint8_t event_id, int port_id, uint64_t value, int64_t ts);
extern esp_err_t ws_handler(httpd_req_t *req);
static const char *TAG = "http";

static QueueHandle_t event_queue = NULL;

static char response_buffer[512];
static int response_len = 0;
static int parsed_rele1 = 0;
static int parsed_rele2 = 0;
static int parsed_rele3 = 0;
static int parsed_buzzer = 0;
static int parsed_led = 0;
static char parsed_tipo_habilitacion[2];
static char parsed_ind_rechazo[2];

static uint32_t bundle_read_u32_le(const uint8_t *p);
static bool bundle_is_safe_name(const char *name);

static const char *get_content_type(const char *uri)
{
    if (strstr(uri, ".js"))
        return "application/javascript";
    if (strstr(uri, ".css"))
        return "text/css";
    if (strstr(uri, ".html"))
        return "text/html";
    return "text/plain";
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (response_len + evt->data_len < sizeof(response_buffer) - 1)
        {
            memcpy(response_buffer + response_len, evt->data, evt->data_len);
            response_len += evt->data_len;
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        response_buffer[response_len] = '\0';
        parsed_tipo_habilitacion[0] = 0;
        parsed_ind_rechazo[0] = 0;
        cJSON *root = cJSON_Parse(response_buffer);
        if (root)
        {
            cJSON *lector = cJSON_GetObjectItem(root, "lector");

            if (cJSON_IsObject(lector))
            {
                cJSON *rele1 = cJSON_GetObjectItem(lector, "rele1");
                cJSON *rele2 = cJSON_GetObjectItem(lector, "rele2");
                cJSON *rele3 = cJSON_GetObjectItem(lector, "rele3");
                cJSON *buzzer = cJSON_GetObjectItem(lector, "buzzer");
                cJSON *led = cJSON_GetObjectItem(lector, "led");
                cJSON *tipo_habilitacion = cJSON_GetObjectItem(lector, "tipo_habilitacion");
                cJSON *ind_rechazo = cJSON_GetObjectItem(lector, "ind_rechazo");

                parsed_rele1 = rele1 ? atoi(rele1->valuestring) : 0;
                parsed_rele2 = rele2 ? atoi(rele2->valuestring) : 0;
                parsed_rele3 = rele3 ? atoi(rele3->valuestring) : 0;
                parsed_buzzer = buzzer ? atoi(buzzer->valuestring) : 0;
                parsed_led = led ? atoi(led->valuestring) : 0;
                strncpy(parsed_tipo_habilitacion, (char *)tipo_habilitacion->valuestring, 1);
                strncpy(parsed_ind_rechazo, (char *)ind_rechazo->valuestring, 1);

                ESP_LOGI(TAG,
                         "resp: %s ",
                         response_buffer);

                ESP_LOGI(TAG,
                         "rele1=%d rele2=%d rele3=%d buzzer=%d led=%d tipo_habilitacion=%s ind_rechazo=%s ",
                         parsed_rele1, parsed_rele2, parsed_rele3,
                         parsed_buzzer, parsed_led, parsed_tipo_habilitacion, parsed_ind_rechazo);
            }

            cJSON_Delete(root);
        }
        else
        {
            ESP_LOGE(TAG, "Invalid JSON response");
        }

        response_len = 0;
        break;

    default:
        break;
    }

    return ESP_OK;
}

esp_err_t send_json(uint8_t event_id, uint8_t port_id, uint64_t value, uint32_t timeout)
{
    config_load(&g_config);

    esp_http_client_config_t config = {
        .url = g_config.url_n33bec,
        .timeout_ms = timeout,
        //.event_handler = http_event_handler,
        //        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    char post_data[512];
    char str_value[32];

    if (event_id == 9 || event_id == 10 || event_id == 11)
    {
        uint32_t raw_wiegand = (uint32_t)value;

        uint8_t facility = (raw_wiegand >> 17) & 0xFF; // Bits 17 a 24 (8 bits)
        uint16_t card = (raw_wiegand >> 1) & 0xFFFF;   // Bits 1 a 16 (16 bits)

        snprintf(str_value, sizeof(str_value), "%03u-%05u", facility, card);

        ESP_LOGI(TAG, "Tarjeta Wiegand26 -> RAW: %llu, FC: %u, Card: %u, String: %s",
                 (unsigned long long)value, facility, card, str_value);
    }
    else
    {
        snprintf(str_value, sizeof(str_value), "%llu", (unsigned long long)value);
    }

    snprintf(post_data, sizeof(post_data),
             "{\"cod_tema\":\"%s/%u/%d/%d\",\"valor\":\"%s\",\"event_id\":\"%d\",\"check_card\":\"%d\"}",
             g_config.cod_tema,
             (unsigned int)g_config.device_id,
             (int)event_id,
             (int)port_id,
             str_value,
             (int)event_id,
             (event_id == 9) ? 1 : 0);

    ESP_LOGI(TAG, "Send to N33BEC %s, content = %s", g_config.url_n33bec, post_data);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status_code = esp_http_client_get_status_code(client);

        if (status_code != 200 && status_code != 201)
            err = ESP_FAIL;
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);

    return err;
}

esp_http_client_handle_t handle_send_card = NULL;
esp_err_t send_json_card(uint8_t event_id, uint8_t port_id, uint64_t value, uint32_t timeout, bool *ok, char *tipo_habilitacion)
{
    if (handle_send_card == NULL)
    {
        config_load(&g_config);
        *ok = false;
        esp_http_client_config_t config = {
            .url = g_config.url_n33bec,
            .timeout_ms = timeout,
            .event_handler = http_event_handler,
            .keep_alive_enable = true,
            .keep_alive_interval = 5
            //        .skip_cert_common_name_check = true,
        };

        handle_send_card = esp_http_client_init(&config);

        esp_http_client_set_method(handle_send_card, HTTP_METHOD_POST);
        esp_http_client_set_header(handle_send_card, "Content-Type", "application/json");
    }
    char post_data[384];

    if (event_id == 9 || event_id == 10 || event_id == 11)
    {
        uint32_t raw_wiegand = (uint32_t)value;
        char str_value[32];
        uint8_t facility = (raw_wiegand >> 17) & 0xFF; // Bits 17 a 24 (8 bits)
        uint16_t card = (raw_wiegand >> 1) & 0xFFFF;   // Bits 1 a 16 (16 bits)

        snprintf(str_value, sizeof(str_value), "%03u-%05u", facility, card);

        ESP_LOGI(TAG, "Tarjeta Wiegand26 -> RAW: %llu, FC: %u, Card: %u, String: %s",
                 (unsigned long long)value, facility, card, str_value);
    }

    snprintf(post_data, sizeof(post_data),
             "{\"cod_tema\":\"%s/%u/%d/%d\",\"valor\":\"%llu\",\"event_id\":\"%d\",\"check_card\":\"%d\"}",
             g_config.cod_tema,
             (unsigned int)g_config.device_id,
             (int)event_id,
             (int)port_id,
             value,
             (int)event_id,
             (event_id == 9) ? 1 : 0);

    ESP_LOGI(TAG, "Send to N33BEC %s, content = %s", g_config.url_n33bec, post_data);

    // esp_http_client_set_post_field(handle_send_card, NULL,0);
    esp_http_client_set_post_field(handle_send_card, post_data, strlen(post_data));

    response_len = 0;
    parsed_rele1 = parsed_rele2 = parsed_rele3 = parsed_buzzer = parsed_led = 0;
    parsed_ind_rechazo[0] = 0;
    parsed_tipo_habilitacion[0] = 0;

    esp_err_t err = esp_http_client_perform(handle_send_card);

    if (err == ESP_OK)
    {
        int status_code = esp_http_client_get_status_code(handle_send_card);

        // 204 No Content -> Valid Permanent Card
        // 206 Partial Content -> Valid Temporary Card
        // 404 Not Found -> Card not valid / does not exist

        if (status_code != 200 && status_code != 201)
        {
            err = ESP_FAIL;
        }
        else
        {
            ESP_LOGI(TAG,
                     "HTTP POST Response parsed: rele1=%d rele2=%d rele3=%d "
                     "buzzer=%d led=%d tipo_habilitacion=%s ind_rechazo=%s",
                     parsed_rele1,
                     parsed_rele2,
                     parsed_rele3,
                     parsed_buzzer,
                     parsed_led,
                     parsed_tipo_habilitacion,
                     parsed_ind_rechazo);

            *ok = (parsed_ind_rechazo[0] == 79);
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }
    tipo_habilitacion[0]=parsed_tipo_habilitacion[0];
    if (err != ESP_OK)
    {
        esp_http_client_cleanup(handle_send_card);
        vTaskDelay(pdMS_TO_TICKS(1));
        handle_send_card = NULL;
    }
    return err;
}

static esp_err_t static_file_handler(httpd_req_t *req)
{
    char filepath[516];

    if (strcmp(req->uri, "/") == 0)
    {
        snprintf(filepath, sizeof(filepath), "/fs/index.html");
    }
    else
    {
        snprintf(filepath, sizeof(filepath), "/fs%s", req->uri);
    }
    ESP_LOGW(TAG, "Input URI: %s, Filepath: %s", req->uri, filepath);

    FILE *f = fopen(filepath, "r");
    if (!f)
    {
        ESP_LOGW(TAG, "File not found: %s", filepath);
        return httpd_resp_send_404(req);
    }

    httpd_resp_set_type(req, get_content_type(filepath));

    char buf[256];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)))
    {
        httpd_resp_send_chunk(req, buf, r);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t del_card(httpd_req_t *req)
{
    char buf[64];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len < 0)
        len = 0;
    buf[len] = 0;

    uint64_t id = strtoull(buf, NULL, 10);
    card_del(id);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t get_logs(httpd_req_t *req)
{
    return log_read_all_json(req);
}
/*
static esp_err_t get_logs(httpd_req_t *req)
{

    char *json = log_read_all_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);

    return ESP_OK;
}*/

static esp_err_t get_cards(httpd_req_t *req)
{

    char *json = card_read_all_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);

    return ESP_OK;
}

static esp_err_t add_card(httpd_req_t *req)
{
    char buf[64];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len < 0)
        len = 0;

    buf[len] = 0;
    uint64_t id = strtoull(buf, NULL, 10);
    card_add(id);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t simulate_card(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0)
    {
        httpd_resp_sendstr(req, "ERR: recv");
        return ESP_FAIL;
    }
    if (len < 0)
        len = 0;
    buf[len] = 0;

    cJSON *json = cJSON_Parse(buf);
    if (!json)
    {
        httpd_resp_sendstr(req, "ERR: invalid json");
        return ESP_FAIL;
    }

    cJSON *card_item = cJSON_GetObjectItemCaseSensitive(json, "card");
    cJSON *reader_item = cJSON_GetObjectItemCaseSensitive(json, "reader");

    if (!cJSON_IsNumber(card_item) || !cJSON_IsNumber(reader_item))
    {
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "ERR: invalid fields");
        return ESP_FAIL;
    }

    uint64_t card_value = card_item->valuedouble;
    int port_id = reader_item->valuedouble;

    cJSON_Delete(json);

    evt_t e = {.card = card_value, .port_id = port_id};

    if (xQueueSendToBack(event_queue, &e, 0) != pdTRUE)
        ESP_LOGW(TAG, "wiegand_tsk: event queue full, card=%llu", card_value);
    else
        ESP_LOGI(TAG, "wiegand_tsk: queued card=%llu from reader %d", card_value, port_id);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t test_relay(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0)
    {
        httpd_resp_sendstr(req, "ERR: recv");
        return ESP_FAIL;
    }
    if (len < 0)
        len = 0;
    buf[len] = 0;

    cJSON *json = cJSON_Parse(buf);
    if (!json)
    {
        httpd_resp_sendstr(req, "ERR: invalid json");
        return ESP_FAIL;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, "target");
    if (!cJSON_IsString(item))
    {
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "ERR: invalid target");
        return ESP_FAIL;
    }

    const char *target = item->valuestring;
    uint8_t event_id = 0;
    uint8_t port_id = 0;
    uint64_t value = 0;

    config_load(&g_config);


    if (strcmp(target, "rex1") == 0)
    {
        pulse_output(relay_number_to_gpio(g_config.rex1_relay_number), g_config.rex1_relay_duration_ms);
        event_id = 6;
        port_id = 1;
    }
    else if (strcmp(target, "rex2") == 0)
    {
        pulse_output(relay_number_to_gpio(g_config.rex2_relay_number), g_config.rex2_relay_duration_ms);
        event_id = 6;
        port_id = 2;
    }
    else if (strcmp(target, "port1") == 0)
    {
        pulse_output(relay_number_to_gpio(g_config.port1_relay_number), g_config.port1_relay_duration_ms);
        event_id = 10;
        port_id = 1;
    }
    else if (strcmp(target, "port2") == 0)
    {
        pulse_output(relay_number_to_gpio(g_config.port2_relay_number), g_config.port2_relay_duration_ms);
        event_id = 10;
        port_id = 2;
    }
    else
    {
        cJSON_Delete(json);
        httpd_resp_sendstr(req, "ERR: unknown target");
        return ESP_FAIL;
    }

    dispatch_log_event(event_id, port_id, value, 0);
    cJSON_Delete(json);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t post_config(httpd_req_t *req)
{
    size_t len = req->content_len;
    if (len == 0 || len > 4096)
    {
        httpd_resp_sendstr(req, "ERR: invalid content length");
        return ESP_FAIL;
    }

    char *buf = malloc(len + 1);
    if (!buf)
    {
        httpd_resp_sendstr(req, "ERR: alloc");
        return ESP_FAIL;
    }

    int r = httpd_req_recv(req, buf, len);
    if (r <= 0)
    {
        free(buf);
        httpd_resp_sendstr(req, "ERR: recv");
        return ESP_FAIL;
    }
    if (r < 0)
        r = 0;
    buf[r] = 0;

    config_t cfg;
    if (config_load(&cfg) != ESP_OK)
    {
        // start from defaults if load fails
    }

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json)
    {
        httpd_resp_sendstr(req, "ERR: invalid json");
        return ESP_FAIL;
    }

    cJSON *item = NULL;
    item = cJSON_GetObjectItemCaseSensitive(json, "rex1_relay_gpio");
    if (cJSON_IsNumber(item)) {
        uint8_t relay_num = (uint8_t)item->valuedouble;
        if (relay_num >= 1 && relay_num <= 3)
            cfg.rex1_relay_number = relay_num;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "rex2_relay_gpio");
    if (cJSON_IsNumber(item)) {
        uint8_t relay_num = (uint8_t)item->valuedouble;
        if (relay_num >= 1 && relay_num <= 3)
            cfg.rex2_relay_number = relay_num;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "port1_relay_gpio");
    if (cJSON_IsNumber(item)) {
        uint8_t relay_num = (uint8_t)item->valuedouble;
        if (relay_num >= 1 && relay_num <= 3)
            cfg.port1_relay_number = relay_num;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "port2_relay_gpio");
    if (cJSON_IsNumber(item)) {
        uint8_t relay_num = (uint8_t)item->valuedouble;
        if (relay_num >= 1 && relay_num <= 3)
            cfg.port2_relay_number = relay_num;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "input_debounce_ms");
    if (cJSON_IsNumber(item))
        cfg.input_debounce_ms = (uint32_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "device_id");
    if (cJSON_IsNumber(item))
        cfg.device_id = (uint32_t)item->valuedouble;

    item = cJSON_GetObjectItemCaseSensitive(json, "rex1_relay_duration_ms");
    if (cJSON_IsNumber(item))
        cfg.rex1_relay_duration_ms = (uint32_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "rex2_relay_duration_ms");
    if (cJSON_IsNumber(item))
        cfg.rex2_relay_duration_ms = (uint32_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "port1_relay_duration_ms");
    if (cJSON_IsNumber(item))
        cfg.port1_relay_duration_ms = (uint32_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "port2_relay_duration_ms");
    if (cJSON_IsNumber(item))
        cfg.port2_relay_duration_ms = (uint32_t)item->valuedouble;

    item = cJSON_GetObjectItemCaseSensitive(json, "url_n33bec");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(cfg.url_n33bec, item->valuestring, sizeof(cfg.url_n33bec) - 1);
    cfg.url_n33bec[sizeof(cfg.url_n33bec) - 1] = '\0'; // Ensure null termination

    item = cJSON_GetObjectItemCaseSensitive(json, "cod_tema");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(cfg.cod_tema, item->valuestring, sizeof(cfg.cod_tema) - 1);
    cfg.cod_tema[sizeof(cfg.cod_tema) - 1] = '\0'; // Ensure null termination

    item = cJSON_GetObjectItemCaseSensitive(json, "keep_alive_secs");
    if (cJSON_IsNumber(item))
        cfg.keep_alive_secs = (uint32_t)item->valuedouble;

    cJSON_Delete(json);

    if (config_save(&cfg) != ESP_OK)
    {
        httpd_resp_sendstr(req, "ERR: save failed");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t get_version(httpd_req_t *req)
{
    size_t total_bytes = 0;
    size_t used_bytes = 0;
    size_t free_bytes = 0;

    if (esp_littlefs_info("storage", &total_bytes, &used_bytes) == ESP_OK && total_bytes >= used_bytes)
    {
        free_bytes = total_bytes - used_bytes;
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();

    char version_json[256];
    snprintf(version_json, sizeof(version_json),
             "{\"version\":\"%s\",\"fs_total_bytes\":%u,\"fs_free_bytes\":%u,\"date\":\"%s\",\"time\":\"%s\",\"version_ota\":\"%s\"}",
             PROJECT_VERSION, (unsigned)total_bytes, (unsigned)free_bytes, app_desc->date, app_desc->time, app_desc->version);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, version_json);
    return ESP_OK;
}

static esp_err_t get_config(httpd_req_t *req)
{
    config_t cfg;
    if (config_load(&cfg) != ESP_OK)
    {
        ESP_LOGW(TAG, "get_config: using defaults");
    }

    cJSON *json = cJSON_CreateObject();
    if (!json)
    {
        httpd_resp_sendstr(req, "ERR: alloc json");
        return ESP_FAIL;
    }

    cJSON_AddNumberToObject(json, "rex1_relay_gpio", cfg.rex1_relay_number);
    cJSON_AddNumberToObject(json, "rex2_relay_gpio", cfg.rex2_relay_number);
    cJSON_AddNumberToObject(json, "port1_relay_gpio", cfg.port1_relay_number);
    cJSON_AddNumberToObject(json, "port2_relay_gpio", cfg.port2_relay_number);
    cJSON_AddNumberToObject(json, "rex1_relay_duration_ms", cfg.rex1_relay_duration_ms);
    cJSON_AddNumberToObject(json, "rex2_relay_duration_ms", cfg.rex2_relay_duration_ms);
    cJSON_AddNumberToObject(json, "port1_relay_duration_ms", cfg.port1_relay_duration_ms);
    cJSON_AddNumberToObject(json, "port2_relay_duration_ms", cfg.port2_relay_duration_ms);
    cJSON_AddStringToObject(json, "url_n33bec", cfg.url_n33bec);
    cJSON_AddStringToObject(json, "cod_tema", cfg.cod_tema);
    cJSON_AddNumberToObject(json, "input_debounce_ms", cfg.input_debounce_ms);
    cJSON_AddNumberToObject(json, "device_id", cfg.device_id);
    cJSON_AddNumberToObject(json, "keep_alive_secs", cfg.keep_alive_secs);

    char *s = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!s)
    {
        httpd_resp_sendstr(req, "ERR: print json");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s);
    free(s);
    return ESP_OK;
}

static const char *wifi_status_to_str(wifi_status_t status)
{
    switch (status)
    {
    case WIFI_STATUS_DISCONNECTED:
        return "disconnected";
    case WIFI_STATUS_DPP_LISTENING:
        return "dpp_listening";
    case WIFI_STATUS_DPP_READY:
        return "dpp_ready";
    case WIFI_STATUS_CONNECTING:
        return "connecting";
    case WIFI_STATUS_CONNECTED:
        return "connected";
    case WIFI_STATUS_DPP_FAILED:
        return "dpp_failed";
    default:
        return "disconnected";
    }
}

static esp_err_t get_wifi_status(httpd_req_t *req)
{
    char uri[512] = {0};
    char ssid[33] = {0};
    char ip[16] = {0};

    wifi_get_dpp_uri(uri, sizeof(uri));
    wifi_get_ssid(ssid, sizeof(ssid));
    wifi_get_ip(ip, sizeof(ip));

    cJSON *json = cJSON_CreateObject();
    if (!json)
    {
        httpd_resp_sendstr(req, "ERR: alloc json");
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(json, "status", wifi_status_to_str(wifi_get_status()));
    cJSON_AddBoolToObject(json, "connected", wifi_is_connected());
    cJSON_AddStringToObject(json, "ssid", ssid);
    cJSON_AddStringToObject(json, "ip", ip);
    cJSON_AddStringToObject(json, "dpp_uri", uri);

    char *s = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!s)
    {
        httpd_resp_sendstr(req, "ERR: print json");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s);
    free(s);
    return ESP_OK;
}

static esp_err_t dpp_bootstrap_handler(httpd_req_t *req)
{
    esp_err_t err = dpp_trigger_bootstrap();
    httpd_resp_set_type(req, "text/plain");
    if (err == ESP_OK)
    {
        httpd_resp_sendstr(req, "OK");
    }
    else
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "ERR: %s", esp_err_to_name(err));
        httpd_resp_sendstr(req, buf);
    }
    return ESP_OK;
}

#define UPDATE_BUNDLE_MAGIC "ACN2"
#define UPDATE_BUNDLE_VERSION 1

static uint32_t bundle_read_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool bundle_is_safe_name(const char *name)
{
    if (!name || name[0] == '\0')
    {
        return false;
    }

    if (strchr(name, '/') || strchr(name, '\\'))
    {
        return false;
    }

    if (strstr(name, ".."))
    {
        return false;
    }

    return true;
}

static esp_err_t read_exact(httpd_req_t *req, uint8_t *buf, size_t len)
{
    size_t received = 0;
    while (received < len)
    {
        int recv_len = httpd_req_recv(req, (char *)buf + received, len - received);
        if (recv_len <= 0)
        {
            ESP_LOGE(TAG, "Failed to receive %u bytes from request", (unsigned)len);
            return ESP_FAIL;
        }
        if (recv_len < 0)
            recv_len = 0;
        received += (size_t)recv_len;
    }
    return ESP_OK;
}

static esp_err_t skip_exact(httpd_req_t *req, size_t len)
{
    uint8_t buf[256];
    size_t remaining = len;
    while (remaining > 0)
    {
        size_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        if (read_exact(req, buf, chunk) != ESP_OK)
        {
            return ESP_FAIL;
        }
        remaining -= chunk;
    }
    return ESP_OK;
}

static esp_err_t apply_web_bundle_stream(httpd_req_t *req, size_t content_len)
{
    if (content_len == 0 || content_len > 512 * 1024)
    {
        ESP_LOGE(TAG, "Invalid bundle size: %u", (unsigned)content_len);
        httpd_resp_sendstr(req, "ERR: invalid bundle size");
        return ESP_FAIL;
    }

    uint8_t header[12];
    if (read_exact(req, header, sizeof(header)) != ESP_OK)
    {
        httpd_resp_sendstr(req, "ERR: bundle recv");
        return ESP_FAIL;
    }

    if (memcmp(header, "WAB1", 4) != 0)
    {
        httpd_resp_sendstr(req, "ERR: invalid bundle format");
        return ESP_FAIL;
    }

    uint32_t version = bundle_read_u32_le(header + 4);
    if (version != 1)
    {
        httpd_resp_sendstr(req, "ERR: unsupported bundle version");
        return ESP_FAIL;
    }

    uint32_t file_count = bundle_read_u32_le(header + 8);
    uint32_t written = 0;
    size_t consumed = 12;

    for (uint32_t i = 0; i < file_count; ++i)
    {
        if (consumed + 4 > content_len)
        {
            httpd_resp_sendstr(req, "ERR: truncated bundle header");
            return ESP_FAIL;
        }

        uint8_t name_len_buf[4];
        if (read_exact(req, name_len_buf, sizeof(name_len_buf)) != ESP_OK)
        {
            httpd_resp_sendstr(req, "ERR: bundle recv");
            return ESP_FAIL;
        }
        uint32_t name_len = bundle_read_u32_le(name_len_buf);
        consumed += 4;

        if (consumed + name_len > content_len)
        {
            httpd_resp_sendstr(req, "ERR: truncated bundle filename");
            return ESP_FAIL;
        }

        char filename[128];
        if (name_len >= sizeof(filename))
        {
            httpd_resp_sendstr(req, "ERR: filename too long");
            return ESP_FAIL;
        }

        if (read_exact(req, (uint8_t *)filename, name_len) != ESP_OK)
        {
            httpd_resp_sendstr(req, "ERR: bundle recv");
            return ESP_FAIL;
        }
        filename[name_len] = '\0';
        consumed += name_len;

        uint8_t data_len_buf[4];
        if (read_exact(req, data_len_buf, sizeof(data_len_buf)) != ESP_OK)
        {
            httpd_resp_sendstr(req, "ERR: bundle recv");
            return ESP_FAIL;
        }
        uint32_t data_len = bundle_read_u32_le(data_len_buf);
        consumed += 4;

        if (consumed + data_len > content_len)
        {
            httpd_resp_sendstr(req, "ERR: truncated bundle data");
            return ESP_FAIL;
        }

        if (strcmp(filename, "log.dat") == 0 || strcmp(filename, "log.txt") == 0)
        {
            consumed += data_len;
            if (skip_exact(req, data_len) != ESP_OK)
            {
                httpd_resp_sendstr(req, "ERR: bundle recv");
                return ESP_FAIL;
            }
            continue;
        }

        if (!bundle_is_safe_name(filename))
        {
            ESP_LOGW(TAG, "Ignoring unsafe bundle entry: %s", filename);
            consumed += data_len;
            if (skip_exact(req, data_len) != ESP_OK)
            {
                httpd_resp_sendstr(req, "ERR: bundle recv");
                return ESP_FAIL;
            }
            continue;
        }

        char filepath[160];
        snprintf(filepath, sizeof(filepath), "/fs/%s", filename);

        FILE *f = fopen(filepath, "wb");
        if (!f)
        {
            ESP_LOGE(TAG, "Failed to open bundle file for write: %s", filepath);
            httpd_resp_sendstr(req, "ERR: open bundle file");
            return ESP_FAIL;
        }

        uint8_t buf[1024];
        size_t remaining = data_len;
        while (remaining > 0)
        {
            size_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
            if (read_exact(req, buf, chunk) != ESP_OK)
            {
                fclose(f);
                httpd_resp_sendstr(req, "ERR: bundle recv");
                return ESP_FAIL;
            }
            if (fwrite(buf, 1, chunk, f) != chunk)
            {
                ESP_LOGE(TAG, "Failed to write bundle file: %s", filepath);
                fclose(f);
                httpd_resp_sendstr(req, "ERR: write bundle file");
                return ESP_FAIL;
            }
            remaining -= chunk;
        }

        fclose(f);
        consumed += data_len;
        written++;
    }

    ESP_LOGI(TAG, "Web bundle updated: %u files", written);
    return ESP_OK;
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "OK: rebooting");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t get_info(httpd_req_t *req)
{
    config_t cfg;
    if (config_load(&cfg) != ESP_OK)
    {
        ESP_LOGW(TAG, "get_info: using defaults");
    }

    char wifi_ip[16] = {0};
    wifi_get_ip(wifi_ip, sizeof(wifi_ip));

    char eth_ip[16] = {0};
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
    esp_netif_t *eth_netif = get_example_netif_from_desc(EXAMPLE_NETIF_DESC_ETH);
    if (eth_netif)
    {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(eth_netif, &ip_info) == ESP_OK)
        {
            snprintf(eth_ip, sizeof(eth_ip), IPSTR, IP2STR(&ip_info.ip));
        }
    }
#endif

    uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000);

    cJSON *json = cJSON_CreateObject();
    if (!json)
    {
        httpd_resp_sendstr(req, "ERR: alloc json");
        return ESP_FAIL;
    }

    cJSON_AddNumberToObject(json, "device_id", cfg.device_id);
    cJSON_AddStringToObject(json, "wifi_ip", wifi_ip);
    cJSON_AddStringToObject(json, "eth_ip", eth_ip);
    cJSON_AddNumberToObject(json, "uptime_sec", uptime_sec);

    char *s = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!s)
    {
        httpd_resp_sendstr(req, "ERR: print json");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s);
    free(s);
    return ESP_OK;
}

static esp_err_t get_device_info(httpd_req_t *req)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    char mac_str[18];
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    // Tamaño de la flash

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

#if CONFIG_ESPTOOLPY_FLASHFREQ_40M
    const char *flash_speed_str = "40MHz";
#elif CONFIG_ESPTOOLPY_FLASHFREQ_80M
    const char *flash_speed_str = "80MHz";
#elif CONFIG_ESPTOOLPY_FLASHFREQ_120M
    const char *flash_speed_str = "120MHz";
#else
    const char *flash_speed_str = "unknown";
#endif

#if CONFIG_ESPTOOLPY_FLASHMODE_QIO
    const char *flash_mode_str = "QIO";
#elif CONFIG_ESPTOOLPY_FLASHMODE_QOUT
    const char *flash_mode_str = "QOUT";
#elif CONFIG_ESPTOOLPY_FLASHMODE_DIO
    const char *flash_mode_str = "DIO";
#elif CONFIG_ESPTOOLPY_FLASHMODE_DOUT
    const char *flash_mode_str = "DOUT";
#else
    const char *flash_mode_str = "unknown";
#endif

    char device_info_json[512];
    snprintf(device_info_json, sizeof(device_info_json),
             "{\"mac\":\"%s\",\"chip_model\":\"%s\",\"chip_cores\":%d,\"chip_revision\":%d,\"sdk_version\":\"%s\",\"free_heap\":%lu,\"min_free_heap\":%lu,\"flash_size\":%lu,\"flash_speed\":\"%s\",\"flash_mode\":\"%s\"}",
             mac_str,
             (chip_info.model == CHIP_ESP32) ? "ESP32" : (chip_info.model == CHIP_ESP32S2) ? "ESP32S2"
                                                     : (chip_info.model == CHIP_ESP32S3)   ? "ESP32S3"
                                                     : (chip_info.model == CHIP_ESP32C3)   ? "ESP32C3"
                                                     : (chip_info.model == CHIP_ESP32C2)   ? "ESP32C2"
                                                     : (chip_info.model == CHIP_ESP32C6)   ? "ESP32C6"
                                                                                           : "Unknown",
             chip_info.cores,
             chip_info.revision,
             esp_get_idf_version(),
             (unsigned long)free_heap,
             (unsigned long)min_free_heap,
             (unsigned long)flash_size,
             flash_speed_str,
             flash_mode_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, device_info_json);
    return ESP_OK;
}

static esp_err_t ota_handler(httpd_req_t *req)
{
    size_t content_len = req->content_len;
    if (content_len == 0 || content_len > 4 * 1024 * 1024)
    {
        ESP_LOGE(TAG, "Invalid update bundle size: %u", (unsigned)content_len);
        httpd_resp_sendstr(req, "ERR: invalid update size");
        return ESP_FAIL;
    }

    uint8_t header[16];
    if (read_exact(req, header, sizeof(header)) != ESP_OK)
    {
        httpd_resp_sendstr(req, "ERR: update recv");
        return ESP_FAIL;
    }

    if (memcmp(header, UPDATE_BUNDLE_MAGIC, 4) != 0)
    {
        httpd_resp_sendstr(req, "ERR: invalid update bundle format");
        return ESP_FAIL;
    }

    uint32_t version = bundle_read_u32_le(header + 4);
    if (version != UPDATE_BUNDLE_VERSION)
    {
        httpd_resp_sendstr(req, "ERR: unsupported update bundle version");
        return ESP_FAIL;
    }

    uint32_t firmware_len = bundle_read_u32_le(header + 8);
    uint32_t web_bundle_len = bundle_read_u32_le(header + 12);
    if (firmware_len == 0 || web_bundle_len == 0)
    {
        httpd_resp_sendstr(req, "ERR: invalid update bundle layout");
        return ESP_FAIL;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition)
    {
        ESP_LOGE(TAG, "No OTA partition available");
        httpd_resp_sendstr(req, "ERR: no ota partition");
        return ESP_FAIL;
    }

    if (firmware_len > update_partition->size)
    {
        ESP_LOGE(TAG, "Firmware image exceeds OTA partition size");
        httpd_resp_sendstr(req, "ERR: image too large");
        return ESP_FAIL;
    }

    esp_ota_handle_t update_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_sendstr(req, "ERR: ota begin");
        return ESP_FAIL;
    }

    uint8_t buf[1024];
    size_t total = 0;
    size_t remaining = firmware_len;

    while (remaining > 0)
    {
        size_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        if (read_exact(req, buf, chunk) != ESP_OK)
        {
            esp_ota_abort(update_handle);
            httpd_resp_sendstr(req, "ERR: update recv");
            return ESP_FAIL;
        }

        err = esp_ota_write(update_handle, buf, chunk);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            httpd_resp_sendstr(req, "ERR: ota write");
            return ESP_FAIL;
        }

        remaining -= chunk;
        total += chunk;
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_sendstr(req, "ERR: ota end");
        return ESP_FAIL;
    }

    if (apply_web_bundle_stream(req, web_bundle_len) != ESP_OK)
    {
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_sendstr(req, "ERR: ota boot");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA image accepted, %u bytes", (unsigned)total);
    httpd_resp_sendstr(req, "OK: update prepared");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t web_bundle_handler(httpd_req_t *req)
{
    size_t content_len = req->content_len;
    if (content_len == 0 || content_len > 512 * 1024)
    {
        ESP_LOGE(TAG, "Invalid bundle size: %u", (unsigned)content_len);
        httpd_resp_sendstr(req, "ERR: invalid bundle size");
        return ESP_FAIL;
    }

    esp_err_t err = apply_web_bundle_stream(req, content_len);
    if (err == ESP_OK)
    {
        httpd_resp_sendstr(req, "OK: web bundle updated");
    }
    return err;
}

void http_init(QueueHandle_t qh)
{
    ESP_LOGI(TAG, "Initializing HTTP server");
    httpd_handle_t s;

    event_queue = qh;

    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.uri_match_fn = httpd_uri_match_wildcard;

    c.max_open_sockets = 7;
    c.max_uri_handlers = 25;
    c.lru_purge_enable = true;

    c.stack_size = 8192;

    ESP_LOGI(TAG, "Starting server on port: '%d'", c.server_port);
    if (httpd_start(&s, &c) == ESP_OK)
    {
        httpd_uri_t put_uri = {
            .uri = "/card",
            .method = HTTP_PUT,
            .handler = add_card};

        httpd_uri_t del_uri = {
            .uri = "/card",
            .method = HTTP_DELETE,
            .handler = del_card};

        httpd_uri_t cards_uri = {
            .uri = "/cards",
            .method = HTTP_GET,
            .handler = get_cards};

        httpd_uri_t logs_uri = {
            .uri = "/logs",
            .method = HTTP_GET,
            .handler = get_logs};

        httpd_uri_t cfg_uri = {
            .uri = "/config",
            .method = HTTP_POST,
            .handler = post_config};

        httpd_uri_t simulate_card_uri = {
            .uri = "/simulate",
            .method = HTTP_POST,
            .handler = simulate_card};

        httpd_uri_t get_cfg_uri = {
            .uri = "/config",
            .method = HTTP_GET,
            .handler = get_config};

        httpd_uri_t version_uri = {
            .uri = "/version",
            .method = HTTP_GET,
            .handler = get_version};

        httpd_uri_t ota_uri = {
            .uri = "/ota",
            .method = HTTP_POST,
            .handler = ota_handler};

        httpd_uri_t reboot_uri = {
            .uri = "/reboot",
            .method = HTTP_POST,
            .handler = reboot_handler};

        httpd_uri_t bundle_uri = {
            .uri = "/storage",
            .method = HTTP_POST,
            .handler = web_bundle_handler};

        httpd_uri_t u1 = {.uri = "/", .method = HTTP_GET, .handler = static_file_handler};
        httpd_register_uri_handler(s, &u1);
        httpd_register_uri_handler(s, &put_uri);
        httpd_register_uri_handler(s, &del_uri);
        httpd_register_uri_handler(s, &logs_uri);
        httpd_register_uri_handler(s, &cards_uri);
        httpd_register_uri_handler(s, &cfg_uri);
        httpd_register_uri_handler(s, &get_cfg_uri);
        httpd_register_uri_handler(s, &version_uri);
        httpd_register_uri_handler(s, &ota_uri);
        httpd_register_uri_handler(s, &reboot_uri);

        httpd_uri_t info_uri = {
            .uri = "/info",
            .method = HTTP_GET,
            .handler = get_info};
        httpd_register_uri_handler(s, &info_uri);

        httpd_uri_t device_info_uri = {
            .uri = "/device_info",
            .method = HTTP_GET,
            .handler = get_device_info};
        httpd_register_uri_handler(s, &device_info_uri);
        httpd_register_uri_handler(s, &bundle_uri);
        httpd_register_uri_handler(s, &simulate_card_uri);

        httpd_uri_t test_relay_uri = {
            .uri = "/test",
            .method = HTTP_POST,
            .handler = test_relay};
        httpd_register_uri_handler(s, &test_relay_uri);

        httpd_uri_t wifi_uri = {.uri = "/wifi", .method = HTTP_GET, .handler = get_wifi_status};
        httpd_register_uri_handler(s, &wifi_uri);

        httpd_uri_t dpp_bs_uri = {.uri = "/dpp/bootstrap", .method = HTTP_POST, .handler = dpp_bootstrap_handler};
        httpd_register_uri_handler(s, &dpp_bs_uri);

        httpd_uri_t ws_uri = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler,
            .is_websocket = true
        };
        httpd_register_uri_handler(s, &ws_uri);

        httpd_uri_t static_files = {.uri = "/*", .method = HTTP_GET, .handler = static_file_handler};
        httpd_register_uri_handler(s, &static_files);

        ws_init(s);

        ESP_LOGI(TAG, "End Initializing HTTP server");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start HTTP server!");
    }
}
