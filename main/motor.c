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
 * Motor-control task.
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
#define MOTOR_FWD_MAX_PULSE_US              1000U
#define MOTOR_FWD_START_PULSE_US            1430U

#define MOTOR_STOP_PULSE_US                 1500U

#define MOTOR_BCK_START_PULSE_US            1541U
#define MOTOR_BCK_MAX_PULSE_US              2000U

/*
 * Burst pulses.
 *
 * These values are intentionally stronger than the minimum
 * continuous movement pulses. This helps to overcome static
 * friction and restart after neutral frames.
 */
#define MOTOR_FWD_BURST_PULSE_US            1380U
#define MOTOR_BCK_BURST_PULSE_US            1590U

/*
 * Standard DS04-NFC servo signal:
 *
 * 20 ms = 50 Hz.
 */
#define MOTOR_PWM_PERIOD_US                 20000U
#define MOTOR_CONTROL_PERIOD_MS             20U

/*
 * Speeds from 1% through 20% use burst packets.
 *
 * Speeds from 21% through 100% use continuous pulse-width control.
 */
#define MOTOR_BURST_MAX_SPEED_PERCENT       20U

/*
 * Number of consecutive movement frames in each burst packet.
 *
 * 3 frames x 20 ms = 60 ms movement packet.
 */
#define MOTOR_BURST_MOVE_FRAMES             3U

/*
 * Neutral time when changing direction.
 *
 * 4 frames x 20 ms = 80 ms at 1500 us.
 *
 * Reduce to 3U for 60 ms if 80 ms feels too slow.
 * Increase to 5U if the mechanism still kicks during reversal.
 */
#define MOTOR_DIRECTION_CHANGE_STOP_FRAMES  4U

/*
 * Motor task configuration.
 */
#define MOTOR_CONTROL_TASK_STACK_SIZE       3072U
#define MOTOR_CONTROL_TASK_PRIORITY         5U

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

typedef enum
{
    MOTOR_TRANSITION_NONE = 0,
    MOTOR_TRANSITION_NEUTRAL

} motor_transition_t;

/*
 * Current command requested by the application.
 *
 * When requested_direction is STOPPED and speed is greater than
 * zero, the speed is only preset. It will be used by the next
 * move_fwd() or move_bck() call.
 */
static motor_direction_t requested_direction =
    MOTOR_DIRECTION_STOPPED;

static uint8_t requested_speed_percent = 0U;

/*
 * Clamp a percentage to the valid public range.
 */
static uint8_t motor_clamp_speed(uint8_t speed_percent)
{
    if (speed_percent > 100U)
    {
        return 100U;
    }

    return speed_percent;
}

/*
 * Set the MCPWM comparator value.
 *
 * MCPWM timer resolution is 1 MHz:
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
 * Allow MCPWM events to control the output GPIO.
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
 * This is only used during initialization.
 *
 * During normal operation, the servo is stopped by continuously
 * sending its calibrated neutral pulse.
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
 * Wake the motor-control task after changing the command.
 */
static void motor_notify_control_task(void)
{
    if (motor_control_task_handle != NULL)
    {
        xTaskNotifyGive(motor_control_task_handle);
    }
}

/*
 * Return true only for a direct forward/backward reversal.
 *
 * STOPPED -> FORWARD and STOPPED -> BACKWARD are not reversals.
 */
static bool motor_is_reversal(
    motor_direction_t current_direction,
    motor_direction_t requested_direction_value)
{
    return
        (current_direction != MOTOR_DIRECTION_STOPPED) &&
        (requested_direction_value != MOTOR_DIRECTION_STOPPED) &&
        (current_direction != requested_direction_value);
}

/*
 * Return the pulse used during a low-speed movement packet.
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
 * Input mapping:
 *
 *   21%  -> minimum reliable continuous movement
 *   100% -> maximum movement
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

    speed_percent = motor_clamp_speed(speed_percent);

    const uint32_t continuous_min_speed =
        MOTOR_BURST_MAX_SPEED_PERCENT + 1U;

    /*
     * For the current configuration:
     *
     *   100 - 21 = 79 interpolation intervals.
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

        return
            MOTOR_FWD_START_PULSE_US -
            ((pulse_range * position) / input_range);
    }

    if (direction == MOTOR_DIRECTION_BACKWARD)
    {
        const uint32_t pulse_range =
            MOTOR_BCK_MAX_PULSE_US -
            MOTOR_BCK_START_PULSE_US;

        return
            MOTOR_BCK_START_PULSE_US +
            ((pulse_range * position) / input_range);
    }

    return MOTOR_STOP_PULSE_US;
}

/*
 * Convert the low-speed command into burst duty.
 *
 * Mapping:
 *
 *   1%  ->   5% duty
 *   2%  ->  10% duty
 *   5%  ->  25% duty
 *   10% ->  50% duty
 *   12% ->  60% duty
 *   20% -> 100% duty
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
 * Calculate how many neutral frames must follow each movement
 * packet.
 *
 * Formula:
 *
 *   total_frames =
 *       ceil(move_frames * 100 / density)
 *
 *   stop_frames =
 *       total_frames - move_frames
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

    const uint32_t numerator =
        MOTOR_BURST_MOVE_FRAMES * 100U;

    /*
     * Integer ceiling division:
     *
     *   ceil(numerator / density)
     */
    const uint32_t total_frames =
        (numerator + density - 1U) / density;

    if (total_frames <= MOTOR_BURST_MOVE_FRAMES)
    {
        return 0U;
    }

    return total_frames - MOTOR_BURST_MOVE_FRAMES;
}

/*
 * Log the selected burst configuration.
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
 * Reset a burst sequence so the next frame starts a complete
 * movement packet.
 */
static void motor_reset_burst_sequence(
    motor_burst_phase_t *burst_phase,
    uint32_t *burst_frames_remaining)
{
    *burst_phase = MOTOR_BURST_PHASE_MOVE;
    *burst_frames_remaining = MOTOR_BURST_MOVE_FRAMES;
}

/*
 * Motor-control task.
 *
 * This task owns the direction actually being applied to the servo.
 *
 * For low speeds:
 *
 *   MOVE MOVE MOVE STOP STOP ...
 *
 * For high speeds:
 *
 *   continuous movement pulse
 *
 * For a direction reversal:
 *
 *   current direction
 *   -> 1500 us for 80 ms
 *   -> opposite direction
 */
static void motor_control_task(void *arg)
{
    (void)arg;

    /*
     * Direction currently allowed to reach the servo.
     *
     * This is intentionally separate from requested_direction.
     */
    motor_direction_t applied_direction =
        MOTOR_DIRECTION_STOPPED;

    uint8_t applied_speed_percent = 0U;

    motor_burst_phase_t burst_phase =
        MOTOR_BURST_PHASE_MOVE;

    uint32_t burst_frames_remaining =
        MOTOR_BURST_MOVE_FRAMES;

    motor_transition_t transition =
        MOTOR_TRANSITION_NONE;

    uint32_t transition_frames_remaining = 0U;

    /*
     * Destination command saved while neutral transition is active.
     */
    motor_direction_t transition_target_direction =
        MOTOR_DIRECTION_STOPPED;

    uint8_t transition_target_speed = 0U;

    while (true)
    {
        motor_direction_t direction;
        uint8_t speed_percent;

        motor_get_requested_state(
            &direction,
            &speed_percent);

        speed_percent = motor_clamp_speed(speed_percent);

        /*
         * A zero-speed request is always considered STOPPED.
         */
        if (speed_percent == 0U)
        {
            direction = MOTOR_DIRECTION_STOPPED;
        }

        /*
         * ========================================================
         * HANDLE A COMMAND WHILE NEUTRAL TRANSITION IS ACTIVE
         * ========================================================
         */
        if (transition == MOTOR_TRANSITION_NEUTRAL)
        {
            /*
             * Always track the newest command.
             *
             * If the application changes its mind while the motor
             * is neutral, the newest command becomes the target.
             */
            transition_target_direction = direction;
            transition_target_speed = speed_percent;

            /*
             * A real STOP cancels the direction transition.
             */
            if ((direction == MOTOR_DIRECTION_STOPPED) ||
                (speed_percent == 0U))
            {
                transition = MOTOR_TRANSITION_NONE;
                transition_frames_remaining = 0U;

                applied_direction =
                    MOTOR_DIRECTION_STOPPED;

                applied_speed_percent = 0U;

                motor_reset_burst_sequence(
                    &burst_phase,
                    &burst_frames_remaining);

                motor_set_pulse_us(
                    MOTOR_STOP_PULSE_US);

                ulTaskNotifyTake(
                    pdTRUE,
                    pdMS_TO_TICKS(
                        MOTOR_CONTROL_PERIOD_MS));

                continue;
            }

            /*
             * Keep neutral for the configured number of complete
             * control frames.
             */
            motor_set_pulse_us(
                MOTOR_STOP_PULSE_US);

            if (transition_frames_remaining > 0U)
            {
                transition_frames_remaining--;
            }

            if (transition_frames_remaining == 0U)
            {
                transition =
                    MOTOR_TRANSITION_NONE;

                applied_direction =
                    transition_target_direction;

                applied_speed_percent =
                    transition_target_speed;

                motor_reset_burst_sequence(
                    &burst_phase,
                    &burst_frames_remaining);

                ESP_LOGI(
                    TAG,
                    "Direction transition complete: "
                    "direction=%d, speed=%u%%",
                    (int)applied_direction,
                    (unsigned)applied_speed_percent);
            }

            ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(
                    MOTOR_CONTROL_PERIOD_MS));

            continue;
        }

        /*
         * ========================================================
         * DETECT A NEW DIRECTION REVERSAL
         * ========================================================
         */
        if (motor_is_reversal(
                applied_direction,
                direction))
        {
            transition =
                MOTOR_TRANSITION_NEUTRAL;

            transition_frames_remaining =
                MOTOR_DIRECTION_CHANGE_STOP_FRAMES;

            transition_target_direction =
                direction;

            transition_target_speed =
                speed_percent;

            /*
             * From this point, no movement direction is considered
             * applied. Only neutral is sent.
             */
            const motor_direction_t old_direction =
                applied_direction;

            applied_direction =
                MOTOR_DIRECTION_STOPPED;

            applied_speed_percent = 0U;

            motor_reset_burst_sequence(
                &burst_phase,
                &burst_frames_remaining);

            motor_set_pulse_us(
                MOTOR_STOP_PULSE_US);

            ESP_LOGI(
                TAG,
                "Direction reversal %d -> %d: "
                "neutral for %u ms",
                (int)old_direction,
                (int)direction,
                (unsigned)(
                    MOTOR_DIRECTION_CHANGE_STOP_FRAMES *
                    MOTOR_CONTROL_PERIOD_MS));

            ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(
                    MOTOR_CONTROL_PERIOD_MS));

            continue;
        }

        /*
         * ========================================================
         * NORMAL COMMAND CHANGE
         * ========================================================
         */
        const bool command_changed =
            (direction != applied_direction) ||
            (speed_percent != applied_speed_percent);

        if (command_changed)
        {
            applied_direction = direction;
            applied_speed_percent = speed_percent;

            motor_reset_burst_sequence(
                &burst_phase,
                &burst_frames_remaining);
        }

        uint32_t pulse_us =
            MOTOR_STOP_PULSE_US;

        /*
         * ========================================================
         * STOPPED
         * ========================================================
         */
        if ((applied_direction ==
             MOTOR_DIRECTION_STOPPED) ||
            (applied_speed_percent == 0U))
        {
            pulse_us = MOTOR_STOP_PULSE_US;

            motor_reset_burst_sequence(
                &burst_phase,
                &burst_frames_remaining);
        }

        /*
         * ========================================================
         * BURST-PACKET MODE
         * ========================================================
         */
        else if (applied_speed_percent <=
                 MOTOR_BURST_MAX_SPEED_PERCENT)
        {
            if (burst_frames_remaining == 0U)
            {
                if (burst_phase ==
                    MOTOR_BURST_PHASE_MOVE)
                {
                    const uint32_t stop_frames =
                        motor_calculate_burst_stop_frames(
                            applied_speed_percent);

                    if (stop_frames == 0U)
                    {
                        /*
                         * At 20%, burst density is 100%.
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

            if (burst_phase ==
                MOTOR_BURST_PHASE_MOVE)
            {
                pulse_us =
                    motor_get_burst_pulse(
                        applied_direction);
            }
            else
            {
                pulse_us =
                    MOTOR_STOP_PULSE_US;
            }

            if (burst_frames_remaining > 0U)
            {
                burst_frames_remaining--;
            }
        }

        /*
         * ========================================================
         * CONTINUOUS MODE
         * ========================================================
         */
        else
        {
            pulse_us =
                motor_calculate_continuous_pulse(
                    applied_direction,
                    applied_speed_percent);

            motor_reset_burst_sequence(
                &burst_phase,
                &burst_frames_remaining);
        }

        motor_set_pulse_us(pulse_us);

        /*
         * Wait one servo frame or wake immediately after a command
         * notification.
         */
        ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(
                MOTOR_CONTROL_PERIOD_MS));
    }
}

/*
 * Store a new motor command.
 *
 * The control task is the only code that applies movement pulses.
 * This prevents motor_apply_command() and the task from writing
 * conflicting pulses during a direction change.
 */
static void motor_apply_command(
    motor_direction_t direction,
    uint8_t speed_percent)
{
    speed_percent =
        motor_clamp_speed(speed_percent);

    if (speed_percent == 0U)
    {
        direction =
            MOTOR_DIRECTION_STOPPED;
    }

    motor_store_requested_state(
        direction,
        speed_percent);

    /*
     * Do not directly apply a movement pulse here.
     *
     * Applying it here would bypass the neutral reversal sequence.
     */
    if (direction == MOTOR_DIRECTION_STOPPED)
    {
        motor_set_pulse_us(
            MOTOR_STOP_PULSE_US);
    }

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
        const uint32_t pulse_us =
            motor_calculate_continuous_pulse(
                direction,
                speed_percent);

        ESP_LOGI(
            TAG,
            "Direction=%d, speed=%u%%, "
            "mode=CONTINUOUS, pulse=%lu us",
            (int)direction,
            (unsigned)speed_percent,
            (unsigned long)pulse_us);
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
     * Apply comparator changes at timer-empty.
     *
     * This prevents the pulse width from changing in the middle
     * of a servo frame.
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
     * GPIO HIGH at the beginning of each PWM frame.
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
     * Start at the calibrated neutral pulse.
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
     * Start sending neutral servo frames.
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
        "Motor initialized: period=%u us, "
        "frequency=50 Hz, neutral=%u us, "
        "reversal neutral=%u ms",
        (unsigned)MOTOR_PWM_PERIOD_US,
        (unsigned)MOTOR_STOP_PULSE_US,
        (unsigned)(
            MOTOR_DIRECTION_CHANGE_STOP_FRAMES *
            MOTOR_CONTROL_PERIOD_MS));
}

void move_fwd(void)
{
    if (timer == NULL)
    {
        motor_init();
    }

    uint8_t speed_percent;

    portENTER_CRITICAL(&motor_lock);

    speed_percent =
        requested_speed_percent;

    portEXIT_CRITICAL(&motor_lock);

    /*
     * If no speed was configured, start at 10%.
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

    speed_percent =
        requested_speed_percent;

    portEXIT_CRITICAL(&motor_lock);

    /*
     * If no speed was configured, start at 10%.
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

    speed_percent =
        motor_clamp_speed(speed_percent);

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
     * When stopped, save the speed as a preset without moving.
     */
    if (direction == MOTOR_DIRECTION_STOPPED)
    {
        motor_store_requested_state(
            MOTOR_DIRECTION_STOPPED,
            speed_percent);

        motor_set_pulse_us(
            MOTOR_STOP_PULSE_US);

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

    speed_percent =
        requested_speed_percent;

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
     * Store STOP before applying neutral.
     */
    motor_store_requested_state(
        MOTOR_DIRECTION_STOPPED,
        0U);

    /*
     * Apply neutral immediately.
     */
    motor_set_pulse_us(
        MOTOR_STOP_PULSE_US);

    motor_pwm_enable();

    /*
     * Wake the control task so any burst or reversal transition
     * is cancelled immediately.
     */
    motor_notify_control_task();

    ESP_LOGI(
        TAG,
        "Motor stopped at neutral pulse %u us",
        (unsigned)MOTOR_STOP_PULSE_US);
}