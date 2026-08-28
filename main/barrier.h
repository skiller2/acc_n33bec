#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

void barrier_init(void);
void force_broadcast_barrier(void);
void barrier_trigger_open(void);
void barrier_position_reached_up(void);
void barrier_position_reached_down(void);
void barrier_task(void *arg);
void barrier_set_simulated(bool loop, bool finish_up, bool finish_down);

#ifdef __cplusplus
}
#endif
