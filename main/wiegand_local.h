/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint64_t card; uint32_t reader; } evt_t;
void wiegand_init(int d0, int d1, int reader, int gpio_buzzer, QueueHandle_t qh);


#ifdef __cplusplus
}
#endif
