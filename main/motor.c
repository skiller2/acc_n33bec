#include "motor.h"
#include "config.h"
#include <driver/mcpwm_prelude.h>
#include "esp_log.h"

static const char *TAG = "MOTOR";

static mcpwm_cmpr_handle_t comparator = NULL;
static mcpwm_gen_handle_t generator = NULL;
static mcpwm_oper_handle_t oper = NULL;
static mcpwm_timer_handle_t timer = NULL;

static uint32_t last_pulse_us = 2000;

static void motor_set_pulse_us(uint32_t pulse_us)
{
    if (pulse_us < 500) pulse_us = 500;
    if (pulse_us > 2500) pulse_us = 2500;
    /* For 360 continuous rotation servo - never stop, keep running */
    if (pulse_us > 1450 && pulse_us < 1550) {
        /* Near neutral - keep running at minimum speed in last direction */
        if (last_pulse_us >= 1500) {
            pulse_us = 1550; /* minimum forward */
        } else {
            pulse_us = 1450; /* minimum backward */
        }
    }
    last_pulse_us = pulse_us;
    if (comparator) {
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, pulse_us));
    }
}

void motor_init(void)
{
    if (timer) {
        return;
    }

    ESP_LOGI(TAG, "Init MCPWM on GPIO %d", PWMSERVO_GPIO);

    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, /* 1MHz -> 1us per tick */
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = 20000,     /* 20ms -> 50Hz */
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));

    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = PWMSERVO_GPIO,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator,
                        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator,
                        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_LOW)));

    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, 1520));

    last_pulse_us = 1520;

    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
    }
void move_fwd(void)
{
    if (!timer) {
        motor_init();
    }
    ESP_LOGI(TAG, "Moving forward");
    motor_set_pulse_us(1800);
}

void move_bck(void)
{
    if (!timer) {
        motor_init();
    }
    ESP_LOGI(TAG, "Moving backward");
    motor_set_pulse_us(1200);
}

void motor_stp(void)
{
    if (!timer) {
        motor_init();
    }
    ESP_LOGI(TAG, "Stopping motor");
    /* Keep running for 360 continuous rotation servo - go to minimum speed */
    if (last_pulse_us >= 1500) {
        motor_set_pulse_us(1520);
    } else {
        motor_set_pulse_us(1480);
    }
}
