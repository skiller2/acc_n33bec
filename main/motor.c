#include "motor.h"
#include "config.h"
#include <driver/ledc.h>
#include <esp_err.h>
#include "esp_log.h"

#define SERVO_TIMER LEDC_TIMER_0
#define SERVO_CHANNEL LEDC_CHANNEL_0
#define SERVO_MODE LEDC_LOW_SPEED_MODE
#define SERVO_FREQ_HZ 50
#define SERVO_RESOLUTION LEDC_TIMER_14_BIT

static void servo_set_pulse_us(uint32_t pulse_us)
{
    uint32_t period_us = 1000000 / SERVO_FREQ_HZ;
    uint32_t duty = (pulse_us * ((1 << 14) - 1)) / period_us;
    ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, duty);
    ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
}

void motor_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = SERVO_MODE,
        .timer_num = SERVO_TIMER,
        .duty_resolution = SERVO_RESOLUTION,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num = PWMSERVO_GPIO,
        .speed_mode = SERVO_MODE,
        .channel = SERVO_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&channel);
}

void move_fwd(void)
{
    ESP_LOGI("MOTOR", "Moving forward");
    servo_set_pulse_us(2000);
}

void move_bck(void)
{
    ESP_LOGI("MOTOR", "Moving backward");
    servo_set_pulse_us(1000);
}

void motor_stp(void)
{
    ESP_LOGI("MOTOR", "Stopping motor");
    servo_set_pulse_us(1500);
}
