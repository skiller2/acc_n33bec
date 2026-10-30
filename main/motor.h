#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

void motor_init(void);
void move_fwd(void);
void move_bck(void);

#ifdef __cplusplus
}
#endif
