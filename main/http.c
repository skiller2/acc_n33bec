#include "esp_http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "config.h"
#include "cJSON.h"
#include "wiegand_local.h"
#include "esp_http_client.h"

extern void card_add(uint64_t);
extern void card_del(uint64_t);
extern char *log_read_all_json(void);
extern char *card_read_all_json(void);
static const char *TAG = "http";

static QueueHandle_t event_queue = NULL;

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

esp_err_t  send_json(uint8_t event_id, uint8_t port_id, uint64_t value)
{
    config_load(&g_config);


    esp_http_client_config_t config = {
        .url =             g_config.url_n33bec, 
        .timeout_ms = 1000, // Your server endpoint
        .skip_cert_common_name_check=true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    char post_data[512];

    char str_value[32];

    if (event_id==10) {
        snprintf(str_value,  sizeof(str_value), "000-%llu", value);
    } else {
        snprintf(str_value,  sizeof(str_value), "%llu", value);
    }

    snprintf(post_data, sizeof(post_data),
             "{\"cod_tema\":\"%s/%lu/%d%d\",\"valor\":\"%s\",\"event_id\":\"%d\"}",
             g_config.cod_tema, g_config.device_id, event_id, port_id,str_value, event_id);

    ESP_LOGI(TAG, "Send to N33BEC %s, content = %s", g_config.url_n33bec, post_data);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));

        char response[512];
        int len = esp_http_client_read_response(
            client,
            response,
            sizeof(response) - 1);

        if (len > 0)
        {
            response[len] = '\0';

            ESP_LOGI(TAG, "Response: %s", response);



            cJSON *root = cJSON_Parse(response);

            if (root)
            {
                cJSON *rele1 = cJSON_GetObjectItem(root, "rele1");
                cJSON *rele2 = cJSON_GetObjectItem(root, "rele2");
                cJSON *rele3 = cJSON_GetObjectItem(root, "rele3");


                //if (cJSON_IsNumber(rele1))
                //{
                //   ESP_LOGI(TAG, "success = %d",
                //             cJSON_Number(rele1) );
                //}


                cJSON_Delete(root);
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST failed: %s",
                 esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
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
    int len = httpd_req_recv(req, buf, sizeof(buf));
    buf[len] = 0;

    uint64_t id = strtoull(buf, NULL, 10);
    card_del(id);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t get_logs(httpd_req_t *req)
{

    char *json = log_read_all_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);

    return ESP_OK;
}

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
    int len = httpd_req_recv(req, buf, 63);
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
    int reader_id = reader_item->valuedouble;

    cJSON_Delete(json);

    evt_t e = {.card = card_value, .reader = reader_id};

    if (xQueueSendToBack(event_queue, &e, 0) != pdTRUE)
        ESP_LOGW(TAG, "wiegand_tsk: event queue full, card=%llu", card_value);
    else
        ESP_LOGI(TAG, "wiegand_tsk: queued card=%llu from reader %d", card_value, reader_id);

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
    if (cJSON_IsNumber(item))
        cfg.rex1_relay_gpio = (gpio_num_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "rex2_relay_gpio");
    if (cJSON_IsNumber(item))
        cfg.rex2_relay_gpio = (gpio_num_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "reader1_relay_gpio");
    if (cJSON_IsNumber(item))
        cfg.reader1_relay_gpio = (gpio_num_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "reader2_relay_gpio");
    if (cJSON_IsNumber(item))
        cfg.reader2_relay_gpio = (gpio_num_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "input_debounce_ms");
    if (cJSON_IsNumber(item))
        cfg.input_debounce_ms = (uint32_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "device_id");
    if (cJSON_IsNumber(item))
        cfg.device_id = (uint8_t)item->valuedouble;

    item = cJSON_GetObjectItemCaseSensitive(json, "rex1_relay_duration_ms");
    if (cJSON_IsNumber(item))
        cfg.rex1_relay_duration_ms = (uint32_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "rex2_relay_duration_ms");
    if (cJSON_IsNumber(item))
        cfg.rex2_relay_duration_ms = (uint32_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "reader1_relay_duration_ms");
    if (cJSON_IsNumber(item))
        cfg.reader1_relay_duration_ms = (uint32_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(json, "reader2_relay_duration_ms");
    if (cJSON_IsNumber(item))
        cfg.reader2_relay_duration_ms = (uint32_t)item->valuedouble;

    item = cJSON_GetObjectItemCaseSensitive(json, "url_n33bec");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(cfg.url_n33bec, item->valuestring, sizeof(cfg.url_n33bec) - 1);
    cfg.url_n33bec[sizeof(cfg.url_n33bec) - 1] = '\0'; // Ensure null termination

    item = cJSON_GetObjectItemCaseSensitive(json, "cod_tema");
    if (cJSON_IsString(item) && (item->valuestring != NULL))
        strncpy(cfg.cod_tema, item->valuestring, sizeof(cfg.cod_tema) - 1);
    cfg.cod_tema[sizeof(cfg.cod_tema) - 1] = '\0'; // Ensure null termination


    cJSON_Delete(json);

    if (config_save(&cfg) != ESP_OK)
    {
        httpd_resp_sendstr(req, "ERR: save failed");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OK");
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

    cJSON_AddNumberToObject(json, "rex1_relay_gpio", cfg.rex1_relay_gpio);
    cJSON_AddNumberToObject(json, "rex2_relay_gpio", cfg.rex2_relay_gpio);
    cJSON_AddNumberToObject(json, "reader1_relay_gpio", cfg.reader1_relay_gpio);
    cJSON_AddNumberToObject(json, "reader2_relay_gpio", cfg.reader2_relay_gpio);
    cJSON_AddNumberToObject(json, "rex1_relay_duration_ms", cfg.rex1_relay_duration_ms);
    cJSON_AddNumberToObject(json, "rex2_relay_duration_ms", cfg.rex2_relay_duration_ms);
    cJSON_AddNumberToObject(json, "reader1_relay_duration_ms", cfg.reader1_relay_duration_ms);
    cJSON_AddNumberToObject(json, "reader2_relay_duration_ms", cfg.reader2_relay_duration_ms);
    cJSON_AddStringToObject(json, "url_n33bec", cfg.url_n33bec);
    cJSON_AddStringToObject(json, "cod_tema", cfg.cod_tema);
    cJSON_AddNumberToObject(json, "input_debounce_ms", cfg.input_debounce_ms);
    cJSON_AddNumberToObject(json, "device_id", cfg.device_id);

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

void http_init(QueueHandle_t qh)
{
    ESP_LOGI(TAG, "Initializing HTTP server");
    httpd_handle_t s;

    event_queue = qh;

    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.max_open_sockets = 3;
    c.max_uri_handlers = 10;
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

        httpd_uri_t u1 = {.uri = "/", .method = HTTP_GET, .handler = static_file_handler};
        httpd_register_uri_handler(s, &u1);

        httpd_uri_t static_js = {.uri = "/app.js", .method = HTTP_GET, .handler = static_file_handler};
        httpd_register_uri_handler(s, &static_js);

        httpd_uri_t static_css = {.uri = "/style.css", .method = HTTP_GET, .handler = static_file_handler};
        httpd_register_uri_handler(s, &static_css);

        httpd_register_uri_handler(s, &put_uri);
        httpd_register_uri_handler(s, &del_uri);
        httpd_register_uri_handler(s, &logs_uri);
        httpd_register_uri_handler(s, &cards_uri);
        httpd_register_uri_handler(s, &cfg_uri);
        httpd_register_uri_handler(s, &get_cfg_uri);
        httpd_register_uri_handler(s, &simulate_card_uri);

        ESP_LOGI(TAG, "End Initializing HTTP server");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start HTTP server!");
    }
}
