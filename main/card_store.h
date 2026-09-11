#pragma once
#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* operaciones normales */
void card_mem_sort(void);
int card_mem_exists(uint64_t id);
void card_mem_del(uint64_t id);
void card_mem_batch_add(const uint64_t *ids, size_t n);
void card_mem_sync(void);
bool card_mem_add(uint64_t id);
/* importación masiva (streaming) */
void card_mem_stream_init(void);
bool card_mem_stream_add(uint64_t id);
bool card_mem_stream_flush(void);
/* inicialización */
void card_store_init(void);
esp_err_t http_send_cards(httpd_req_t *req);



#ifdef __cplusplus
}
#endif