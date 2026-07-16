#pragma once

#include <stdint.h>
#include <esp_err.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTC_TIMESTAMP_UPDATE (10*60*1000) // hours in milliseconds

/**
 * @brief Initialize RTC (DS3231) with I2C on GPIO 1 (SDA) and GPIO 2 (SCL)
 *
 * @return ESP_OK on success, otherwise error code
 */
esp_err_t rtc_app_init(void);

/**
 * @brief Read current time from DS3231 RTC
 *
 * @param time Pointer to time_t structure to store the read time
 * @return ESP_OK on success, otherwise error code
 */
esp_err_t rtc_read_time(time_t *time);

/**
 * @brief Write time to DS3231 RTC
 *
 * @param time Pointer to time_t structure with the time to write
 * @return ESP_OK on success, otherwise error code
 */
esp_err_t rtc_write_time(const time_t *time);

/**
 * @brief Sync time from SNTP server and write to RTC
 *
 * @return ESP_OK on success, otherwise error code
 */
esp_err_t rtc_set_rtc_time(void);

/**
 * @brief Get current time from RTC and set system time
 *
 * @return ESP_OK on success, otherwise error code
 */
esp_err_t rtc_set_system_time(void);


/*@brief Check if RTC is connected
 *
 * @return true if RTC is connected, false otherwise
 */
bool rtc_is_connected(void);

#ifdef __cplusplus
}
#endif
