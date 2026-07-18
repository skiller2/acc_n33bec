
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_add(uint8_t event_id, int port_id, uint64_t value, int64_t ts);
//uint8_t event_id, uint8_t port_id, uint64_t value
//void log_add_door_event(int door_id, int is_open);
//void log_add_rex_event(int door_id);
//void log_add_power_event(int is_ac_fault);
uint64_t getTimeStamp();

#ifdef __cplusplus
}
#endif
