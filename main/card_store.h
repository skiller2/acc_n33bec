#pragma once
#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

void card_add(uint64_t);
void card_del(uint64_t);
void card_truncate(void);
int card_exists(uint64_t);
esp_err_t http_send_cards(httpd_req_t *req);

void card_mem_sort(void);
int card_mem_exists(uint64_t);
void card_mem_add(uint64_t);
void card_mem_del(uint64_t);
void card_mem_batch_add(const uint64_t *ids, size_t n);

#ifdef __cplusplus
}
#endif