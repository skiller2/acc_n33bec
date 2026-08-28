#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "ws.h"
#include "wifi.h"
#include "config.h"
#include "barrier.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "ws";

static const char *wifi_status_to_str(wifi_status_t status)
{
    switch (status) {
    case WIFI_STATUS_DISCONNECTED: return "disconnected";
    case WIFI_STATUS_DPP_LISTENING: return "dpp_listening";
    case WIFI_STATUS_DPP_READY: return "dpp_ready";
    case WIFI_STATUS_CONNECTING: return "connecting";
    case WIFI_STATUS_CONNECTED: return "connected";
    case WIFI_STATUS_DPP_FAILED: return "dpp_failed";
    default: return "disconnected";
    }
}

static httpd_handle_t g_server = NULL;


void ws_broadcast_wifi_status(wifi_status_t status, bool connected, const char *ssid, const char *ip, const char *dpp_uri)
{
    if (!g_server) return;

    cJSON *json = cJSON_CreateObject();
    if (!json) return;

    cJSON *wifi = cJSON_CreateObject();
    if (!wifi) {
        cJSON_Delete(json);
        return;
    }

    cJSON_AddStringToObject(wifi, "type", "wifi");
    cJSON_AddStringToObject(wifi, "status", wifi_status_to_str(status));
    cJSON_AddBoolToObject(wifi, "connected", connected);
    if (ssid && ssid[0]) cJSON_AddStringToObject(wifi, "ssid", ssid);
    if (ip && ip[0]) cJSON_AddStringToObject(wifi, "ip", ip);
    if (dpp_uri && dpp_uri[0]) cJSON_AddStringToObject(wifi, "dpp_uri", dpp_uri);

    cJSON_AddItemToObject(json, "wifi", wifi);

    char *s = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!s) return;

    size_t len = strlen(s);
    if (len == 0 || len >= 512) {
        free(s);
        return;
    }

    static char persist[512];
    memcpy(persist, s, len + 1);
    free(s);

    int fds[8];
    size_t fd_count = sizeof(fds) / sizeof(fds[0]);
    esp_err_t err = httpd_get_client_list(g_server, &fd_count, fds);
    if (err != ESP_OK || fd_count == 0) {
        //ESP_LOGW(TAG, "ws broadcast: no clients");
        return;
    }

    for (int i = 0; i < fd_count; i++) {
        int fd = fds[i];
        if (httpd_ws_get_fd_info(g_server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            continue;
        }

        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)(uintptr_t)persist,
            .len = len
        };

        esp_err_t send_err = httpd_ws_send_frame_async(g_server, fd, &frame);
        if (send_err != ESP_OK) {
            ESP_LOGE(TAG, "ws send failed fd=%d err=%s", fd, esp_err_to_name(send_err));
        }
    }
}


void ws_broadcast_io_status(int door1, int door2, int rex1, int rex2, int ali, int rele1, int rele2, int rele3)
{
    if (!g_server) return;

    cJSON *json = cJSON_CreateObject();
    if (!json) return;

    cJSON *io = cJSON_CreateObject();
    if (!io) {
        cJSON_Delete(json);
        return;
    }

    cJSON_AddNumberToObject(io, "door1", door1);
    cJSON_AddNumberToObject(io, "door2", door2);
    cJSON_AddNumberToObject(io, "rex1", rex1);
    cJSON_AddNumberToObject(io, "rex2", rex2);
    cJSON_AddNumberToObject(io, "ali", ali);
    cJSON_AddNumberToObject(io, "rele1", rele1);
    cJSON_AddNumberToObject(io, "rele2", rele2);
    cJSON_AddNumberToObject(io, "rele3", rele3);

    cJSON_AddItemToObject(json, "io", io);

    char *s = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!s) return;

    size_t len = strlen(s);
    if (len == 0 || len >= 512) {
        free(s);
        return;
    }

    static char persist[512];
    memcpy(persist, s, len + 1);
    free(s);

    int fds[8];
    size_t fd_count = sizeof(fds) / sizeof(fds[0]);
    esp_err_t err = httpd_get_client_list(g_server, &fd_count, fds);
    if (err != ESP_OK || fd_count == 0) {
        return;
    }

    for (size_t i = 0; i < fd_count; i++) {
        int fd = fds[i];
        if (httpd_ws_get_fd_info(g_server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            continue;
        }

        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)(uintptr_t)persist,
            .len = len
        };

        esp_err_t send_err = httpd_ws_send_frame_async(g_server, fd, &frame);
        if (send_err != ESP_OK) {
            ESP_LOGE(TAG, "ws io send failed fd=%d err=%s", fd, esp_err_to_name(send_err));
        }
    }
}

void ws_broadcast_barrier(int rex1, int rex2, int loop, int finish_up, int finish_down, int rele1, int rele2, int rele3, uint32_t time_up_ms, uint32_t time_down_ms, uint32_t open_hold_ms, int position, int loop_active_high)
{
    ESP_LOGI(TAG,"ws_broadcast_barrier");
    if (!g_server) return;

    cJSON *json = cJSON_CreateObject();
    if (!json) return;

    cJSON *io = cJSON_CreateObject();
    if (!io) {
        cJSON_Delete(json);
        return;
    }

    cJSON_AddNumberToObject(io, "rex1", rex1);
    cJSON_AddNumberToObject(io, "rex2", rex2);
    cJSON_AddNumberToObject(io, "loop", loop);
    cJSON_AddNumberToObject(io, "finish_up", finish_up);
    cJSON_AddNumberToObject(io, "finish_down", finish_down);
    cJSON_AddNumberToObject(io, "rele1", rele1);
    cJSON_AddNumberToObject(io, "rele2", rele2);
    cJSON_AddNumberToObject(io, "rele3", rele3);
    cJSON_AddNumberToObject(io, "time_up_ms", time_up_ms);
    cJSON_AddNumberToObject(io, "time_down_ms", time_down_ms);
    cJSON_AddNumberToObject(io, "open_hold_ms", open_hold_ms);
    cJSON_AddNumberToObject(io, "position", position);
    cJSON_AddNumberToObject(io, "loop_active_high", loop_active_high);

    cJSON_AddItemToObject(json, "barrier", io);

    char *s = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!s) return;

    size_t len = strlen(s);
    if (len == 0 || len >= 512) {
        free(s);
        return;
    }

    static char persist[512];
    memcpy(persist, s, len + 1);
    free(s);

    int fds[8];
    size_t fd_count = sizeof(fds) / sizeof(fds[0]);
    esp_err_t err = httpd_get_client_list(g_server, &fd_count, fds);
    if (err != ESP_OK || fd_count == 0) {
        return;
    }

    for (size_t i = 0; i < fd_count; i++) {
        int fd = fds[i];
        if (httpd_ws_get_fd_info(g_server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            continue;
        }

        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)(uintptr_t)persist,
            .len = len
        };

        esp_err_t send_err = httpd_ws_send_frame_async(g_server, fd, &frame);
        if (send_err != ESP_OK) {
            ESP_LOGE(TAG, "ws barrier send failed fd=%d err=%s", fd, esp_err_to_name(send_err));
        }
    }
}

void ws_broadcast_card(uint64_t card, int64_t ts, int ok, char tipo_habilitacion, int64_t time_consuming, int port_id)
{
    if (!g_server) return;

    cJSON *json = cJSON_CreateObject();
    if (!json) return;
    cJSON_AddNumberToObject(json, "card", (unsigned long long)card);
    cJSON_AddNumberToObject(json, "ts", (long long)ts);
    cJSON_AddBoolToObject(json, "ok", ok);
    cJSON_AddStringToObject(json, "tipo_habilitacion", tipo_habilitacion == 84 ? "T" : tipo_habilitacion == 80 ? "P" : "U");
    cJSON_AddNumberToObject(json, "time_consuming", (long long)time_consuming);
    cJSON_AddNumberToObject(json, "port_id", port_id);

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
        //ESP_LOGW(TAG, "ws broadcast: no clients");
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

    //ESP_LOGI(TAG, "ws broadcast: %d websocket clients out of %d total", ws_count, (int)fd_count);

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
    memset(&frame, 0, sizeof(frame));
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);

    if (err == ESP_OK)
    {
        if (frame.type == HTTPD_WS_TYPE_PING)
        {
            httpd_ws_frame_t pong = {
                .type = HTTPD_WS_TYPE_PONG,
                .payload = frame.payload,
                .len = frame.len
            };
            httpd_ws_send_frame(req, &pong);
            //free(frame.payload);
            return ESP_OK;
        }

        if (frame.type == HTTPD_WS_TYPE_CLOSE)
        {
            //free(frame.payload);
            return ESP_OK;
        }
        if (frame.type == HTTPD_WS_TYPE_TEXT)
        {
            uint8_t *buf = malloc(frame.len + 1);
            if (buf)
            {
                frame.payload = buf;
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
                    else if (strcmp((char *)buf, "init") == 0)
                    {
                        wifi_broadcast_state();
                        ws_broadcast_io_status(gpio_get_level(DOOR1_GPIO), gpio_get_level(DOOR2_GPIO), gpio_get_level(REX1_GPIO), gpio_get_level(REX2_GPIO), gpio_get_level(ALI_GPIO), gpio_get_level(RELE1_GPIO), gpio_get_level(RELE2_GPIO), gpio_get_level(RELE3_GPIO));
                        force_broadcast_barrier();
                    }
                }
                free(buf);
            }
        }
    }

    return ESP_OK;
}

void ws_init(httpd_handle_t server)
{
    g_server = server;
    ESP_LOGI(TAG, "WebSocket server initialized");
}
