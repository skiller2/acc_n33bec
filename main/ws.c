#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "ws.h"
#include <string.h>

static const char *TAG = "ws";

static httpd_handle_t g_server = NULL;

void ws_broadcast(uint64_t card, int64_t ts, int ok)
{
    if (!g_server) return;

    cJSON *json = cJSON_CreateObject();
    if (!json) return;
    ESP_LOGI(TAG,"broadcast card %llu",card);
    cJSON_AddNumberToObject(json, "card", (unsigned long long)card);
    cJSON_AddNumberToObject(json, "ts", (long long)ts);
    cJSON_AddBoolToObject(json, "ok", ok);

    char *s = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!s) return;

    size_t len = strlen(s);
    if (len == 0 || len >= 512)
    {
        free(s);
        return;
    }

    static char persist[512];
    memcpy(persist, s, len + 1);
    free(s);

    int fds[8];
    size_t fd_count = sizeof(fds) / sizeof(fds[0]);
    esp_err_t err = httpd_get_client_list(g_server, &fd_count, fds);
    if (err != ESP_OK || fd_count == 0)
    {
        ESP_LOGW(TAG, "ws broadcast: no clients");
        return;
    }

    int ws_count = 0;
    for (int i = 0; i < fd_count; i++)
    {
        if (httpd_ws_get_fd_info(g_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
        {
            ws_count++;
        }
    }

    ESP_LOGI(TAG, "ws broadcast: %d websocket clients out of %d total", ws_count, (int)fd_count);

    for (int i = 0; i < fd_count; i++)
    {
        int fd = fds[i];
        if (httpd_ws_get_fd_info(g_server, fd) != HTTPD_WS_CLIENT_WEBSOCKET)
        {
            continue;
        }

        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)(uintptr_t)persist,
            .len = len
        };

        esp_err_t send_err = httpd_ws_send_frame_async(g_server, fd, &frame);
        if (send_err != ESP_OK)
        {
            ESP_LOGE(TAG, "ws send failed fd=%d err=%s", fd, esp_err_to_name(send_err));
        }
    }
}

esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        return ESP_OK;
    }

    httpd_ws_frame_t frame;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);

    if (err == ESP_OK && frame.type == HTTPD_WS_TYPE_TEXT)
    {
        uint8_t *buf = malloc(frame.len + 1);
        if (buf)
        {
            err = httpd_ws_recv_frame(req, &frame, frame.len);
            if (err == ESP_OK)
            {
                buf[frame.len] = 0;
                if (strcmp((char *)buf, "ping") == 0)
                {
                    httpd_ws_frame_t pong = {
                        .type = HTTPD_WS_TYPE_PONG,
                        .payload = (uint8_t *)"pong",
                        .len = 4
                    };
                    httpd_ws_send_frame(req, &pong);
                }
            }
            free(buf);
        }
    }

    return ESP_OK;
}

void ws_init(httpd_handle_t server)
{
    g_server = server;
    ESP_LOGI(TAG, "WebSocket server initialized");
}
