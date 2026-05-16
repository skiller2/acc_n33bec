#include <stdint.h>
#include "esp_timer.h"

extern int card_exists(uint64_t);
extern void log_add(uint64_t, int64_t, int);
extern void ws_broadcast(uint64_t, int64_t, int);

void process_card(uint64_t id)
{
    int ok = card_exists(id);
    int64_t ts = esp_timer_get_time();
    log_add(id, ts, ok);
    ws_broadcast(id, ts, ok);
}
