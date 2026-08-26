#pragma once

#include <stdint.h>
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ws_handler(httpd_req_t *req);
void ws_init(httpd_handle_t server);
void ws_broadcast(uint64_t card, int64_t ts, int ok);

#ifdef __cplusplus
}
#endif
