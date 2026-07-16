
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_add(uint64_t id, int64_t ts, int reader_id,int ok);
//void log_add_door_event(int door_id, int is_open);
//void log_add_rex_event(int door_id);
//void log_add_power_event(int is_ac_fault);


#ifdef __cplusplus
}
#endif
