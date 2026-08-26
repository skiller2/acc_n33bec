#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "ws.h"
#include <string.h>

static const char *TAG = "ws";

#define MAX_WS_CLIENTS 3

static int client_fds[MAX_WS_CLIENTS] = {0};
static httpd_handle_t g_server = NULL;

static int ws_add_client(int sockfd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (client_fds[i] == sockfd)
        {
            return 0;
        }
    }

    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (client_fds[i] == 0)
        {
            client_fds[i] = sockfd;
            ESP_LOGI(TAG, "WS client connected, fd=%d", sockfd);
            return 0;
        }
    }
    return -1;
}

static void ws_remove_client(int sockfd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (client_fds[i] == sockfd)
        {
            client_fds[i] = 0;
            ESP_LOGI(TAG, "WS client disconnected, fd=%d", sockfd);
            break;
        }
    }
}

static void ws_send_to_all(const char *msg, size_t len)
{
    if (!g_server) return;
    ESP_LOGI(TAG,"ws_send_to_all %s",msg);

    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        int fd = client_fds[i];
        if (fd > 0)
        {
            httpd_ws_frame_t frame = {
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)(uintptr_t)msg,
                .len = len
            };
            ESP_LOGI(TAG,"httpd_ws_send_frame_async %s",msg);

            esp_err_t err = httpd_ws_send_frame_async(g_server, fd, &frame);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "ws send failed fd=%d err=%s", fd, esp_err_to_name(err));
                ws_remove_client(fd);
            }
        }
    }
}

void ws_broadcast(uint64_t card, int64_t ts, int ok)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return;
    ESP_LOGI(TAG,"broadcast card %llu",card);
    cJSON_AddNumberToObject(json, "card", (unsigned long long)card);
    cJSON_AddNumberToObject(json, "ts", (long long)ts);
    cJSON_AddBoolToObject(json, "ok", ok);

    char *s = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (s)
    {
        size_t len = strlen(s);
        if (len > 0 && len < 512)
        {
            static char persist[512];
            memcpy(persist, s, len + 1);
            ws_send_to_all(persist, len);
        }
        free(s);
    }
}

esp_err_t ws_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG,"ws_handler");
    int sockfd = httpd_req_to_sockfd(req);

    if (sockfd < 0)
    {
        return ESP_FAIL;
    }

    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG,"ws_add_client");

        ws_add_client(sockfd);
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
