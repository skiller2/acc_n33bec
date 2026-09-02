
#pragma once
#include <stdint.h>
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

void log_add(uint8_t event_id, uint8_t port_id, uint64_t value, int64_t ts);
uint64_t getTimeStamp();

void pending_log_add(uint8_t event_id, uint8_t port_id, uint64_t value, int64_t ts);
void pending_log_load_and_drain(QueueHandle_t q, uint32_t *drained_count);

#ifdef __cplusplus
}
#endif
