
#include "esp_http_server.h"
#include <stdio.h>
#include <string.h>

extern void card_add(uint64_t);

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
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t s;
    httpd_start(&s, &c);
    httpd_uri_t u1 = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    httpd_uri_t u2 = {.uri = "/card", .method = HTTP_PUT, .handler = add_card};
    httpd_register_uri_handler(s, &u1);
    httpd_register_uri_handler(s, &u2);
}
