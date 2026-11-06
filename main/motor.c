#include "motor.h"
#include "config.h"

#include <stdbool.h>
#include <stdint.h>

#include <driver/mcpwm_prelude.h>

#include "esp_check.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"

static const char *TAG = "MOTOR";

/*
 * MCPWM handles
 */
static mcpwm_cmpr_handle_t comparator = NULL;
static mcpwm_gen_handle_t generator = NULL;
static mcpwm_oper_handle_t oper = NULL;
static mcpwm_timer_handle_t timer = NULL;

/*
 * Protects the requested state.
 */
static portMUX_TYPE motor_lock = portMUX_INITIALIZER_UNLOCKED;

#define MOTOR_FWD_MAX_PULSE_US      1000U
#define MOTOR_FWD_START_PULSE_US    1430U

#define MOTOR_STOP_PULSE_US         1500U

#define MOTOR_BCK_START_PULSE_US    1541U
#define MOTOR_BCK_MAX_PULSE_US      2000U


/*
 * Standard servo PWM:
 *
 * 20 ms = 50 Hz
 */
#define MOTOR_PWM_PERIOD_US       5000U

typedef enum
{
    MOTOR_DIRECTION_STOPPED = 0,
    MOTOR_DIRECTION_FORWARD,
    MOTOR_DIRECTION_BACKWARD

} motor_direction_t;

/*
 * Current requested state.
 */
static motor_direction_t requested_direction =
    MOTOR_DIRECTION_STOPPED;

static uint8_t requested_speed_percent = 0;



/*
 * Set comparator pulse width.
 */
static void motor_set_pulse_us(uint32_t pulse_us)
{
    if (comparator == NULL)
    {
        return;
    }

    ESP_ERROR_CHECK(
        mcpwm_comparator_set_compare_value(
            comparator,
            pulse_us));
}

/*
 * Allow MCPWM events to control the GPIO.
 */
static void motor_pwm_enable(void)
{
    if (generator == NULL)
    {
        return;
    }

    ESP_ERROR_CHECK(
        mcpwm_generator_set_force_level(
            generator,
            -1,
            true));
}

/*
 * Force the output LOW.
 *
 * This is only used during initialization or before MCPWM is ready.
 * A continuous-rotation servo should normally be stopped by sending
 * its neutral pulse, not by removing the PWM signal.
 */
static void motor_pwm_disable(void)
{
    if (generator == NULL)
    {
        return;
    }

    ESP_ERROR_CHECK(
        mcpwm_generator_set_force_level(
            generator,
            0,
            true));
}

static uint32_t motor_calculate_pulse(
    motor_direction_t direction,
    uint8_t speed_percent)
{
    if ((direction == MOTOR_DIRECTION_STOPPED) ||
        (speed_percent == 0U))
    {
        return MOTOR_STOP_PULSE_US;
    }

    if (speed_percent > 100U)
    {
        speed_percent = 100U;
    }

    /*
     * Map speed 1..100% to the servo's effective movement range.
     *
     * 0% = STOP (1500 us)
     * 1% = first pulse outside dead band
     * 100% = maximum speed
     */
    const uint32_t position = speed_percent - 1U;

    if (direction == MOTOR_DIRECTION_FORWARD)
    {
        const uint32_t range =
            MOTOR_FWD_START_PULSE_US -
            MOTOR_FWD_MAX_PULSE_US;

        return MOTOR_FWD_START_PULSE_US -
               ((range * position) / 99U);
    }

    if (direction == MOTOR_DIRECTION_BACKWARD)
    {
        const uint32_t range =
            MOTOR_BCK_MAX_PULSE_US -
            MOTOR_BCK_START_PULSE_US;

        return MOTOR_BCK_START_PULSE_US +
               ((range * position) / 99U);
    }

    return MOTOR_STOP_PULSE_US;
}
/*
 * Read state atomically.
 */
static void motor_get_requested_state(
    motor_direction_t *direction,
    uint8_t *speed_percent)
{
    portENTER_CRITICAL(&motor_lock);

    *direction = requested_direction;
    *speed_percent = requested_speed_percent;

    portEXIT_CRITICAL(&motor_lock);
}

/*
 * Store state atomically.
 */
static void motor_store_requested_state(
    motor_direction_t direction,
    uint8_t speed_percent)
{
    portENTER_CRITICAL(&motor_lock);

    requested_direction = direction;
    requested_speed_percent = speed_percent;

    portEXIT_CRITICAL(&motor_lock);
}

/*
 * Apply a motor command directly.
 *
 * No FreeRTOS task is needed because MCPWM continues generating
 * the configured pulse automatically.
 */
static void motor_apply_command(
    motor_direction_t direction,
    uint8_t speed_percent)
{
    if (speed_percent > 100U)
    {
        speed_percent = 100U;
    }

    if (speed_percent == 0U)
    {
        direction = MOTOR_DIRECTION_STOPPED;
    }

    const uint32_t pulse_us =
        motor_calculate_pulse(
            direction,
            speed_percent);

    motor_set_pulse_us(pulse_us);
    motor_pwm_enable();

    motor_store_requested_state(
        direction,
        speed_percent);

    ESP_LOGI(
        TAG,
        "Direction=%d, speed=%u%%, pulse=%lu us",
        (int)direction,
        (unsigned)speed_percent,
        (unsigned long)pulse_us);
}

void motor_init(void)
{
    if (timer != NULL)
    {
        return;
    }

    ESP_LOGI(
        TAG,
        "Initializing continuous-rotation servo on GPIO %d",
        PWMSERVO_GPIO);

    /*
     * Resolution: 1 MHz
     * One tick:   1 us
     * Period:     20000 us
     * Frequency:  50 Hz
     */
    const mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000U,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = MOTOR_PWM_PERIOD_US,
    };

    ESP_ERROR_CHECK(
        mcpwm_new_timer(
            &timer_config,
            &timer));

    const mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };

    ESP_ERROR_CHECK(
        mcpwm_new_operator(
            &operator_config,
            &oper));

    ESP_ERROR_CHECK(
        mcpwm_operator_connect_timer(
            oper,
            timer));

    /*
     * Apply comparator changes at timer-empty so the pulse width
     * cannot change in the middle of a PWM frame.
     */
    const mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };

    ESP_ERROR_CHECK(
        mcpwm_new_comparator(
            oper,
            &comparator_config,
            &comparator));

    const mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = PWMSERVO_GPIO,
    };

    ESP_ERROR_CHECK(
        mcpwm_new_generator(
            oper,
            &generator_config,
            &generator));

    /*
     * GPIO HIGH at the beginning of the PWM frame.
     */
    ESP_ERROR_CHECK(
        mcpwm_generator_set_action_on_timer_event(
            generator,
            MCPWM_GEN_TIMER_EVENT_ACTION(
                MCPWM_TIMER_DIRECTION_UP,
                MCPWM_TIMER_EVENT_EMPTY,
                MCPWM_GEN_ACTION_HIGH)));

    /*
     * GPIO LOW when the comparator is reached.
     */
    ESP_ERROR_CHECK(
        mcpwm_generator_set_action_on_compare_event(
            generator,
            MCPWM_GEN_COMPARE_EVENT_ACTION(
                MCPWM_TIMER_DIRECTION_UP,
                comparator,
                MCPWM_GEN_ACTION_LOW)));

    /*
     * Start at the neutral position.
     */
    ESP_ERROR_CHECK(
        mcpwm_comparator_set_compare_value(
            comparator,
            MOTOR_STOP_PULSE_US));

    /*
     * Keep the GPIO LOW until the timer is running.
     */
    motor_pwm_disable();

    ESP_ERROR_CHECK(
        mcpwm_timer_enable(timer));

    ESP_ERROR_CHECK(
        mcpwm_timer_start_stop(
            timer,
            MCPWM_TIMER_START_NO_STOP));

    /*
     * Send neutral pulses continuously.
     */
    motor_pwm_enable();

    motor_store_requested_state(
        MOTOR_DIRECTION_STOPPED,
        0U);

    ESP_LOGI(
        TAG,
        "Motor initialized: stopped at %u us",
        (unsigned)MOTOR_STOP_PULSE_US);
}

void move_fwd(void)
{
    if (timer == NULL)
    {
        motor_init();
    }

    /*
     * Preserve the currently configured speed.
     * If no speed was configured, start at 10%.
     */
    uint8_t speed_percent;

    portENTER_CRITICAL(&motor_lock);

    speed_percent = requested_speed_percent;

    portEXIT_CRITICAL(&motor_lock);

    if (speed_percent == 0U)
    {
        speed_percent = 10U;
    }

    motor_apply_command(
        MOTOR_DIRECTION_FORWARD,
        speed_percent);
}

void move_bck(void)
{
    if (timer == NULL)
    {
        motor_init();
    }

    /*
     * Preserve the currently configured speed.
     * If no speed was configured, start at 10%.
     */
    uint8_t speed_percent;

    portENTER_CRITICAL(&motor_lock);

    speed_percent = requested_speed_percent;

    portEXIT_CRITICAL(&motor_lock);

    if (speed_percent == 0U)
    {
        speed_percent = 10U;
    }

    motor_apply_command(
        MOTOR_DIRECTION_BACKWARD,
        speed_percent);
}

void motor_set_speed(uint8_t speed_percent)
{
    if (timer == NULL)
    {
        motor_init();
    }

    motor_direction_t direction;
    uint8_t current_speed;

    motor_get_requested_state(
        &direction,
        &current_speed);

    (void)current_speed;

    if (speed_percent == 0U)
    {
        motor_stp();
        return;
    }

    /*
     * If stopped, store the desired speed but continue sending
     * the neutral pulse. The speed will be used by the next
     * move_fwd() or move_bck() call.
     */
    if (direction == MOTOR_DIRECTION_STOPPED)
    {
        motor_store_requested_state(
            MOTOR_DIRECTION_STOPPED,
            speed_percent);

        motor_set_pulse_us(MOTOR_STOP_PULSE_US);
        motor_pwm_enable();

        ESP_LOGI(
            TAG,
            "Speed preset to %u%%; motor remains stopped",
            (unsigned)speed_percent);

        return;
    }

    motor_apply_command(
        direction,
        speed_percent);
}

uint8_t motor_get_speed(void)
{
    uint8_t speed_percent;

    portENTER_CRITICAL(&motor_lock);

    speed_percent = requested_speed_percent;

    portEXIT_CRITICAL(&motor_lock);

    return speed_percent;
}

void motor_stp(void)
{
    if (timer == NULL)
    {
        motor_init();
    }

    /*
     * A continuous-rotation servo is stopped by continuously
     * sending its calibrated neutral pulse.
     */
    motor_set_pulse_us(MOTOR_STOP_PULSE_US);
    motor_pwm_enable();

    motor_store_requested_state(
        MOTOR_DIRECTION_STOPPED,
        0U);

    ESP_LOGI(
        TAG,
        "Motor stopped at neutral pulse %u us",
        (unsigned)MOTOR_STOP_PULSE_US);
}