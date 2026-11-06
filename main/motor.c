#include "motor.h"
#include "config.h"

#include <stdbool.h>
#include <stdint.h>

#include <driver/mcpwm_prelude.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MOTOR";

/*
 * MCPWM handles.
 */
static mcpwm_cmpr_handle_t comparator = NULL;
static mcpwm_gen_handle_t generator = NULL;
static mcpwm_oper_handle_t oper = NULL;
static mcpwm_timer_handle_t timer = NULL;

/*
 * Motor control task.
 */
static TaskHandle_t motor_control_task_handle = NULL;

/*
 * Protects the requested motor state.
 */
static portMUX_TYPE motor_lock = portMUX_INITIALIZER_UNLOCKED;

/*
 * DS04-NFC calibration.
 *
 * Continuous speed range:
 *
 * Forward:
 *   1430 us = minimum reliable continuous movement
 *   1000 us = maximum speed
 *
 * Backward:
 *   1541 us = minimum reliable continuous movement
 *   2000 us = maximum speed
 *
 * Neutral:
 *   1500 us = stopped
 */
#define MOTOR_FWD_MAX_PULSE_US          1000U
#define MOTOR_FWD_START_PULSE_US        1430U

#define MOTOR_STOP_PULSE_US             1500U

#define MOTOR_BCK_START_PULSE_US        1541U
#define MOTOR_BCK_MAX_PULSE_US          2000U

/*
 * Burst pulses.
 *
 * These pulses are intentionally stronger than the minimum
 * continuous movement pulses.
 *
 * The stronger pulse helps overcome:
 *
 * - Static friction
 * - Gearbox friction
 * - Mechanical load
 * - Restarting after neutral frames
 *
 * Tune these values if necessary.
 */
#define MOTOR_FWD_BURST_PULSE_US        1380U
#define MOTOR_BCK_BURST_PULSE_US        1590U

/*
 * Standard servo signal:
 *
 * 20 ms = 50 Hz.
 */
#define MOTOR_PWM_PERIOD_US             20000U
#define MOTOR_CONTROL_PERIOD_MS         20U

/*
 * Speeds from 1% through 20% use burst packets.
 *
 * Speeds from 21% through 100% use continuous pulses.
 */
#define MOTOR_BURST_MAX_SPEED_PERCENT   20U

/*
 * Number of consecutive movement frames in each burst packet.
 *
 * Each frame lasts 20 ms:
 *
 * 3 frames x 20 ms = 60 ms movement packet.
 *
 * Raise this to 4 or 5 if the motor cannot overcome its load.
 * Lower it to 2 if the movement is too abrupt.
 */
#define MOTOR_BURST_MOVE_FRAMES         3U

/*
 * Motor task configuration.
 */
#define MOTOR_CONTROL_TASK_STACK_SIZE   3072U
#define MOTOR_CONTROL_TASK_PRIORITY     5U

typedef enum
{
    MOTOR_DIRECTION_STOPPED = 0,
    MOTOR_DIRECTION_FORWARD,
    MOTOR_DIRECTION_BACKWARD

} motor_direction_t;

typedef enum
{
    MOTOR_BURST_PHASE_MOVE = 0,
    MOTOR_BURST_PHASE_STOP

} motor_burst_phase_t;

/*
 * Current requested motor state.
 *
 * If direction is STOPPED but speed is greater than zero,
 * speed has been preset and will be used by the next call to
 * move_fwd() or move_bck().
 */
static motor_direction_t requested_direction =
    MOTOR_DIRECTION_STOPPED;

static uint8_t requested_speed_percent = 0U;

/*
 * Set the MCPWM comparator value.
 *
 * With a timer resolution of 1 MHz:
 *
 *   1 tick = 1 us
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
 * Allow MCPWM events to control the output.
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
 * Only used during initialization.
 *
 * During normal operation, the servo must be stopped by sending
 * its neutral pulse continuously.
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

/*
 * Atomically read the requested motor state.
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
 * Atomically store the requested motor state.
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
 * Wake the motor-control task after a command changes.
 *
 * The notification causes the task to reload the command and reset
 * its current burst packet immediately.
 */
static void motor_notify_control_task(void)
{
    if (motor_control_task_handle != NULL)
    {
        xTaskNotifyGive(motor_control_task_handle);
    }
}

/*
 * Return the calibrated movement pulse used during a burst packet.
 */
static uint32_t motor_get_burst_pulse(
    motor_direction_t direction)
{
    switch (direction)
    {
        case MOTOR_DIRECTION_FORWARD:
            return MOTOR_FWD_BURST_PULSE_US;

        case MOTOR_DIRECTION_BACKWARD:
            return MOTOR_BCK_BURST_PULSE_US;

        case MOTOR_DIRECTION_STOPPED:
        default:
            return MOTOR_STOP_PULSE_US;
    }
}

/*
 * Calculate the pulse width for continuous operation.
 *
 * Input range:
 *
 *   21%  -> minimum continuous movement pulse
 *   100% -> maximum movement pulse
 */
static uint32_t motor_calculate_continuous_pulse(
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

    const uint32_t continuous_min_speed =
        MOTOR_BURST_MAX_SPEED_PERCENT + 1U;

    /*
     * 21..100 contains 80 command values and 79 intervals.
     */
    const uint32_t input_range =
        100U - continuous_min_speed;

    const uint32_t position =
        speed_percent - continuous_min_speed;

    if (direction == MOTOR_DIRECTION_FORWARD)
    {
        const uint32_t pulse_range =
            MOTOR_FWD_START_PULSE_US -
            MOTOR_FWD_MAX_PULSE_US;

        return MOTOR_FWD_START_PULSE_US -
               ((pulse_range * position) / input_range);
    }

    if (direction == MOTOR_DIRECTION_BACKWARD)
    {
        const uint32_t pulse_range =
            MOTOR_BCK_MAX_PULSE_US -
            MOTOR_BCK_START_PULSE_US;

        return MOTOR_BCK_START_PULSE_US +
               ((pulse_range * position) / input_range);
    }

    return MOTOR_STOP_PULSE_US;
}

/*
 * Convert the low-speed command into an effective burst duty cycle.
 *
 * Requested speed:
 *
 *   1%  -> 5% packet duty
 *   2%  -> 10% packet duty
 *   5%  -> 25% packet duty
 *   10% -> 50% packet duty
 *   12% -> 60% packet duty
 *   20% -> 100% packet duty
 */
static uint32_t motor_calculate_burst_density(
    uint8_t speed_percent)
{
    if (speed_percent == 0U)
    {
        return 0U;
    }

    if (speed_percent > MOTOR_BURST_MAX_SPEED_PERCENT)
    {
        speed_percent = MOTOR_BURST_MAX_SPEED_PERCENT;
    }

    return
        ((uint32_t)speed_percent * 100U) /
        MOTOR_BURST_MAX_SPEED_PERCENT;
}

/*
 * Calculate how many neutral frames must follow each movement packet.
 *
 * Formula:
 *
 *   total_frames =
 *       ceil(move_frames * 100 / density_percent)
 *
 *   stop_frames =
 *       total_frames - move_frames
 *
 * Examples with 3 movement frames:
 *
 *   Speed  1%, density   5%:
 *       total = ceil(3 * 100 / 5) = 60
 *       stop  = 60 - 3 = 57 frames
 *
 *   Speed 10%, density  50%:
 *       total = ceil(3 * 100 / 50) = 6
 *       stop  = 6 - 3 = 3 frames
 *
 *   Speed 12%, density  60%:
 *       total = ceil(3 * 100 / 60) = 5
 *       stop  = 5 - 3 = 2 frames
 *
 *   Speed 20%, density 100%:
 *       total = 3
 *       stop  = 0 frames
 */
static uint32_t motor_calculate_burst_stop_frames(
    uint8_t speed_percent)
{
    const uint32_t density =
        motor_calculate_burst_density(speed_percent);

    if (density == 0U)
    {
        return 0U;
    }

    /*
     * Integer ceiling division:
     *
     * ceil(numerator / denominator)
     */
    const uint32_t numerator =
        MOTOR_BURST_MOVE_FRAMES * 100U;

    const uint32_t total_frames =
        (numerator + density - 1U) / density;

    if (total_frames <= MOTOR_BURST_MOVE_FRAMES)
    {
        return 0U;
    }

    return total_frames - MOTOR_BURST_MOVE_FRAMES;
}

/*
 * Log the calculated low-speed packet timing.
 */
static void motor_log_burst_configuration(
    motor_direction_t direction,
    uint8_t speed_percent)
{
    const uint32_t density =
        motor_calculate_burst_density(speed_percent);

    const uint32_t stop_frames =
        motor_calculate_burst_stop_frames(speed_percent);

    const uint32_t move_time_ms =
        MOTOR_BURST_MOVE_FRAMES *
        MOTOR_CONTROL_PERIOD_MS;

    const uint32_t stop_time_ms =
        stop_frames *
        MOTOR_CONTROL_PERIOD_MS;

    ESP_LOGI(
        TAG,
        "Direction=%d, speed=%u%%, mode=BURST, "
        "density=%lu%%, move=%lu ms, stop=%lu ms, "
        "pulse=%lu us",
        (int)direction,
        (unsigned)speed_percent,
        (unsigned long)density,
        (unsigned long)move_time_ms,
        (unsigned long)stop_time_ms,
        (unsigned long)motor_get_burst_pulse(direction));
}

/*
 * Motor-control task.
 *
 * This task runs once for every 20 ms servo frame.
 *
 * Low speeds:
 *
 *   MOVE MOVE MOVE STOP STOP ...
 *
 * High speeds:
 *
 *   Continuous movement pulse.
 */
static void motor_control_task(void *arg)
{
    (void)arg;

    motor_direction_t active_direction =
        MOTOR_DIRECTION_STOPPED;

    uint8_t active_speed_percent = 0U;

    motor_burst_phase_t burst_phase =
        MOTOR_BURST_PHASE_MOVE;

    uint32_t burst_frames_remaining = 0U;

    TickType_t last_wake_time =
        xTaskGetTickCount();

    while (true)
    {
        motor_direction_t direction;
        uint8_t speed_percent;

        motor_get_requested_state(
            &direction,
            &speed_percent);

        const bool command_changed =
            (direction != active_direction) ||
            (speed_percent != active_speed_percent);

        if (command_changed)
        {
            active_direction = direction;
            active_speed_percent = speed_percent;

            /*
             * Every new low-speed command starts with a full
             * movement packet.
             */
            burst_phase = MOTOR_BURST_PHASE_MOVE;
            burst_frames_remaining =
                MOTOR_BURST_MOVE_FRAMES;
        }

        uint32_t pulse_us = MOTOR_STOP_PULSE_US;

        if ((direction == MOTOR_DIRECTION_STOPPED) ||
            (speed_percent == 0U))
        {
            pulse_us = MOTOR_STOP_PULSE_US;

            burst_phase = MOTOR_BURST_PHASE_MOVE;
            burst_frames_remaining =
                MOTOR_BURST_MOVE_FRAMES;
        }
        else if (speed_percent <=
                 MOTOR_BURST_MAX_SPEED_PERCENT)
        {
            /*
             * Low-speed burst-packet operation.
             */
            if (burst_frames_remaining == 0U)
            {
                if (burst_phase == MOTOR_BURST_PHASE_MOVE)
                {
                    const uint32_t stop_frames =
                        motor_calculate_burst_stop_frames(
                            speed_percent);

                    if (stop_frames == 0U)
                    {
                        /*
                         * At 20%, burst density is 100%.
                         * Restart another movement packet
                         * immediately.
                         */
                        burst_phase =
                            MOTOR_BURST_PHASE_MOVE;

                        burst_frames_remaining =
                            MOTOR_BURST_MOVE_FRAMES;
                    }
                    else
                    {
                        burst_phase =
                            MOTOR_BURST_PHASE_STOP;

                        burst_frames_remaining =
                            stop_frames;
                    }
                }
                else
                {
                    burst_phase =
                        MOTOR_BURST_PHASE_MOVE;

                    burst_frames_remaining =
                        MOTOR_BURST_MOVE_FRAMES;
                }
            }

            if (burst_phase == MOTOR_BURST_PHASE_MOVE)
            {
                pulse_us =
                    motor_get_burst_pulse(direction);
            }
            else
            {
                pulse_us = MOTOR_STOP_PULSE_US;
            }

            if (burst_frames_remaining > 0U)
            {
                burst_frames_remaining--;
            }
        }
        else
        {
            /*
             * Continuous pulse-width operation.
             */
            pulse_us =
                motor_calculate_continuous_pulse(
                    direction,
                    speed_percent);

            burst_phase = MOTOR_BURST_PHASE_MOVE;
            burst_frames_remaining =
                MOTOR_BURST_MOVE_FRAMES;
        }

        motor_set_pulse_us(pulse_us);

        /*
         * Wait until the next 20 ms control point.
         *
         * A task notification interrupts the wait when the command
         * changes. In that case, reset the timing reference and
         * process the new command immediately.
         */
        const uint32_t notified =
            ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(MOTOR_CONTROL_PERIOD_MS));

        if (notified != 0U)
        {
            last_wake_time = xTaskGetTickCount();
            continue;
        }

        /*
         * Keep the variable synchronized for subsequent command
         * notifications. The actual periodic wait is performed by
         * ulTaskNotifyTake().
         */
        last_wake_time = xTaskGetTickCount();
    }
}

/*
 * Store and apply a motor command.
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

    motor_store_requested_state(
        direction,
        speed_percent);

    /*
     * Apply a safe initial pulse immediately.
     *
     * The task continues controlling subsequent frames.
     */
    uint32_t initial_pulse_us = MOTOR_STOP_PULSE_US;

    if ((direction != MOTOR_DIRECTION_STOPPED) &&
        (speed_percent > 0U))
    {
        if (speed_percent <=
            MOTOR_BURST_MAX_SPEED_PERCENT)
        {
            initial_pulse_us =
                motor_get_burst_pulse(direction);
        }
        else
        {
            initial_pulse_us =
                motor_calculate_continuous_pulse(
                    direction,
                    speed_percent);
        }
    }

    motor_set_pulse_us(initial_pulse_us);
    motor_pwm_enable();

    motor_notify_control_task();

    if (direction == MOTOR_DIRECTION_STOPPED)
    {
        ESP_LOGI(
            TAG,
            "Direction=STOPPED, speed=0%%, pulse=%u us",
            (unsigned)MOTOR_STOP_PULSE_US);
    }
    else if (speed_percent <=
             MOTOR_BURST_MAX_SPEED_PERCENT)
    {
        motor_log_burst_configuration(
            direction,
            speed_percent);
    }
    else
    {
        ESP_LOGI(
            TAG,
            "Direction=%d, speed=%u%%, mode=CONTINUOUS, "
            "pulse=%lu us",
            (int)direction,
            (unsigned)speed_percent,
            (unsigned long)initial_pulse_us);
    }
}

void motor_init(void)
{
    if (timer != NULL)
    {
        return;
    }

    ESP_LOGI(
        TAG,
        "Initializing DS04-NFC servo on GPIO %d",
        PWMSERVO_GPIO);

    /*
     * MCPWM configuration:
     *
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
     * Comparator changes are applied when the timer reaches zero.
     * This prevents pulse changes in the middle of a PWM frame.
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
     * Set GPIO HIGH at the beginning of every PWM frame.
     */
    ESP_ERROR_CHECK(
        mcpwm_generator_set_action_on_timer_event(
            generator,
            MCPWM_GEN_TIMER_EVENT_ACTION(
                MCPWM_TIMER_DIRECTION_UP,
                MCPWM_TIMER_EVENT_EMPTY,
                MCPWM_GEN_ACTION_HIGH)));

    /*
     * Set GPIO LOW when the comparator value is reached.
     */
    ESP_ERROR_CHECK(
        mcpwm_generator_set_action_on_compare_event(
            generator,
            MCPWM_GEN_COMPARE_EVENT_ACTION(
                MCPWM_TIMER_DIRECTION_UP,
                comparator,
                MCPWM_GEN_ACTION_LOW)));

    /*
     * Initialize at the neutral pulse.
     */
    ESP_ERROR_CHECK(
        mcpwm_comparator_set_compare_value(
            comparator,
            MOTOR_STOP_PULSE_US));

    motor_pwm_disable();

    ESP_ERROR_CHECK(
        mcpwm_timer_enable(timer));

    ESP_ERROR_CHECK(
        mcpwm_timer_start_stop(
            timer,
            MCPWM_TIMER_START_NO_STOP));

    /*
     * Start sending neutral pulses.
     */
    motor_pwm_enable();

    motor_store_requested_state(
        MOTOR_DIRECTION_STOPPED,
        0U);

    /*
     * Start the motor-control task.
     */
    const BaseType_t task_result =
        xTaskCreate(
            motor_control_task,
            "motor_control",
            MOTOR_CONTROL_TASK_STACK_SIZE,
            NULL,
            MOTOR_CONTROL_TASK_PRIORITY,
            &motor_control_task_handle);

    ESP_ERROR_CHECK(
        task_result == pdPASS
            ? ESP_OK
            : ESP_ERR_NO_MEM);

    ESP_LOGI(
        TAG,
        "Motor initialized: period=%u us, frequency=50 Hz, "
        "neutral=%u us",
        (unsigned)MOTOR_PWM_PERIOD_US,
        (unsigned)MOTOR_STOP_PULSE_US);
}

void move_fwd(void)
{
    if (timer == NULL)
    {
        motor_init();
    }

    uint8_t speed_percent;

    portENTER_CRITICAL(&motor_lock);

    speed_percent = requested_speed_percent;

    portEXIT_CRITICAL(&motor_lock);

    /*
     * If no speed was previously configured, start at 10%.
     */
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

    uint8_t speed_percent;

    portENTER_CRITICAL(&motor_lock);

    speed_percent = requested_speed_percent;

    portEXIT_CRITICAL(&motor_lock);

    /*
     * If no speed was previously configured, start at 10%.
     */
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

    if (speed_percent > 100U)
    {
        speed_percent = 100U;
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
     * When stopped, save the requested speed as a preset without
     * moving the servo.
     */
    if (direction == MOTOR_DIRECTION_STOPPED)
    {
        motor_store_requested_state(
            MOTOR_DIRECTION_STOPPED,
            speed_percent);

        motor_set_pulse_us(MOTOR_STOP_PULSE_US);
        motor_pwm_enable();

        motor_notify_control_task();

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
     * Store STOP before updating the comparator. This prevents the
     * control task from intentionally applying another movement
     * pulse after seeing the old command.
     */
    motor_store_requested_state(
        MOTOR_DIRECTION_STOPPED,
        0U);

    /*
     * Apply neutral immediately.
     */
    motor_set_pulse_us(MOTOR_STOP_PULSE_US);
    motor_pwm_enable();

    /*
     * Wake the control task so it reloads STOP immediately.
     */
    motor_notify_control_task();

    ESP_LOGI(
        TAG,
        "Motor stopped at neutral pulse %u us",
        (unsigned)MOTOR_STOP_PULSE_US);
}