#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void motor_init(void);

void move_fwd(void);
void move_bck(void);
void motor_stp(void);

void motor_set_speed(uint8_t speed_percent);
uint8_t motor_get_speed(void);

#ifdef __cplusplus
}
#endif

#endif