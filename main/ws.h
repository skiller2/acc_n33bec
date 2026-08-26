#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_http_server.h"
#include "wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ws_handler(httpd_req_t *req);
void ws_init(httpd_handle_t server);
void ws_broadcast_card(uint64_t card, int64_t ts, int ok, char tipo_habilitacion, int64_t time_consuming, int port_id);
void ws_broadcast_wifi_status(wifi_status_t status, bool connected, const char *ssid, const char *ip, const char *dpp_uri);

#ifdef __cplusplus
}
#endif
