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
void ws_broadcast_io_status(int door1, int door2, int rex1, int rex2, int ali, int rele1, int rele2, int rele3);
void ws_broadcast_barrier(int rex1, int rex2, int loop, int finish_up, int finish_down, int rele1, int rele2, int rele3, uint32_t time_up_ms, uint32_t time_down_ms, uint32_t open_hold_ms, int position, int loop_active_high, const char *status, uint32_t remaining_ms);

#ifdef __cplusplus
}
#endif
