
#include "esp_http_server.h"
#include <string.h>

static httpd_handle_t server=NULL;

void ws_broadcast(uint64_t id,int64_t ts,int ok){}

static esp_err_t ws_handler(httpd_req_t *req){ return ESP_OK; }

void ws_init(){}
