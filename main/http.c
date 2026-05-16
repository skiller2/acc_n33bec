#include "esp_http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

extern void card_add(uint64_t);
extern void card_del(uint64_t);
extern char *log_read_all_json(void);
static const char *TAG = "http";

static esp_err_t index_handler(httpd_req_t *req)
{
    FILE *f = fopen("/fs/index.html", "r");
    if (!f)
        return httpd_resp_send_404(req);
    char buf[256];
    size_t r;
    while ((r = fread(buf, 1, 256, f)))
        httpd_resp_send_chunk(req, buf, r);
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

void http_init()
{
    ESP_LOGI(TAG, "Initializing HTTP server");
    httpd_handle_t s;

    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.max_open_sockets = 3;
    c.lru_purge_enable = true;             

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

        httpd_uri_t logs_uri = {
            .uri = "/logs",
            .method = HTTP_GET,
            .handler = get_logs};

        httpd_uri_t u1 = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
        httpd_register_uri_handler(s, &u1);

        httpd_register_uri_handler(s, &put_uri);
        httpd_register_uri_handler(s, &del_uri);
        httpd_register_uri_handler(s, &logs_uri);
        ESP_LOGI(TAG, "End Initializing HTTP server");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server!");
    }
}
