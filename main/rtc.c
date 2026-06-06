/*
 * DS3231 RTC driver with I2C interface (New I2C driver)
 * GPIO 1 (SDA), GPIO 2 (SCL)
 */

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif_sntp.h"
#include "rtc.h"

static const char *TAG = "rtc_ds3231";

// DS3231 I2C slave address
#define DS3231_ADDR 0x68

// DS3231 register addresses
#define DS3231_REG_SECONDS 0x00
#define DS3231_REG_MINUTES 0x01
#define DS3231_REG_HOURS 0x02
#define DS3231_REG_DAY_OF_WEEK 0x03
#define DS3231_REG_DATE 0x04
#define DS3231_REG_MONTH_CENTURY 0x05
#define DS3231_REG_YEAR 0x06

// I2C configuration
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_SDA_IO GPIO_NUM_2
#define I2C_MASTER_SCL_IO GPIO_NUM_1
#define I2C_TIMEOUT_MS 1000

static i2c_master_bus_handle_t bus_handle = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;

/**
 * @brief Convert BCD (Binary Coded Decimal) to decimal
 */
static uint8_t bcd_to_decimal(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/**
 * @brief Convert decimal to BCD (Binary Coded Decimal)
 */
static uint8_t decimal_to_bcd(uint8_t decimal)
{
    return ((decimal / 10) << 4) | (decimal % 10);
}

/**
 * @brief Read registers from DS3231
 */
static esp_err_t ds3231_read(uint8_t reg, uint8_t *data, uint16_t len)
{
    if (dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Send register address and read data
    return i2c_master_transmit_receive(dev_handle, &reg, 1, data, len,
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

/**
 * @brief Write registers to DS3231
 */
static esp_err_t ds3231_write(uint8_t reg, const uint8_t *data, uint16_t len)
{
    if (dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t write_buf[len + 1];
    write_buf[0] = reg;
    memcpy(&write_buf[1], data, len);
    
    return i2c_master_transmit(dev_handle, write_buf, len + 1,
                              pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

/**
 * @brief Initialize I2C and DS3231
 */
esp_err_t rtc_app_init(void)
{
    if (dev_handle != NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Init RTC");
    
    // Configure I2C master bus
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&i2c_mst_config, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(err));
        return err;
    }

    // Configure DS3231 device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add DS3231 device to I2C bus: %s", esp_err_to_name(err));
        i2c_del_master_bus(bus_handle);
        bus_handle = NULL;
        return err;
    }

    // Verify DS3231 is responding
    uint8_t reg_val;
    err = ds3231_read(DS3231_REG_SECONDS, &reg_val, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to communicate with DS3231: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(dev_handle);
        i2c_del_master_bus(bus_handle);
        dev_handle = NULL;
        bus_handle = NULL;
        return err;
    }

    ESP_LOGI(TAG, "RTC DS3231 initialized successfully");
    return ESP_OK;
}

/**
 * @brief Read current time from DS3231
 */
esp_err_t rtc_read_time(time_t *time)
{
    if (dev_handle == NULL) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t date_time[7];
    esp_err_t err = ds3231_read(DS3231_REG_SECONDS, date_time, 7);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time from DS3231: %s", esp_err_to_name(err));
        return err;
    }

    // Parse BCD values
    uint8_t seconds = bcd_to_decimal(date_time[0] & 0x7F);
    uint8_t minutes = bcd_to_decimal(date_time[1] & 0x7F);
    uint8_t hours = bcd_to_decimal(date_time[2] & 0x3F);
    uint8_t day = bcd_to_decimal(date_time[3] & 0x07);
    uint8_t date = bcd_to_decimal(date_time[4] & 0x3F);
    uint8_t month = bcd_to_decimal(date_time[5] & 0x1F);
    uint8_t century = (date_time[5] >> 7) & 0x01;
    uint8_t year = bcd_to_decimal(date_time[6]);

    // Convert to full year
    int full_year = 1900 + (century ? 100 : 0) + year;

    // Create struct tm
    struct tm tm_info = {0};
    tm_info.tm_sec = seconds;
    tm_info.tm_min = minutes;
    tm_info.tm_hour = hours;
    tm_info.tm_mday = date;
    tm_info.tm_mon = month - 1;  // tm_mon is 0-11
    tm_info.tm_year = full_year - 1900;
    tm_info.tm_wday = day - 1;   // tm_wday is 0-6 (0=Sunday)
    tm_info.tm_isdst = 0;

    // Convert to time_t
    *time = mktime(&tm_info);
    
    ESP_LOGI(TAG, "Read time from RTC: %04d-%02d-%02d %02d:%02d:%02d",
             full_year, month, date, hours, minutes, seconds);

    return ESP_OK;
}

/**
 * @brief Write time to DS3231
 */
esp_err_t rtc_write_time(const time_t *time)
{
    if (dev_handle == NULL) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Convert time_t to struct tm
    struct tm *tm_info = gmtime(time);
    if (tm_info == NULL) {
        ESP_LOGE(TAG, "Failed to convert time");
        return ESP_ERR_INVALID_ARG;
    }

    // Prepare BCD values
    uint8_t seconds = decimal_to_bcd(tm_info->tm_sec);
    uint8_t minutes = decimal_to_bcd(tm_info->tm_min);
    uint8_t hours = decimal_to_bcd(tm_info->tm_hour);
    uint8_t day_of_week = tm_info->tm_wday + 1;  // Convert from 0-6 to 1-7
    uint8_t date = decimal_to_bcd(tm_info->tm_mday);
    uint8_t month = decimal_to_bcd(tm_info->tm_mon + 1);  // Convert from 0-11 to 1-12
    uint8_t year = decimal_to_bcd((tm_info->tm_year + 1900) % 100);
    
    // Set century bit for years 2000-2099 (bit 7 of month register)
    if ((tm_info->tm_year + 1900) >= 2000 && (tm_info->tm_year + 1900) < 2100) {
        month |= 0x80;
    }

    // Write to DS3231
    uint8_t date_time[7] = {
        seconds,
        minutes,
        hours,
        day_of_week,
        date,
        month,
        year
    };

    esp_err_t err = ds3231_write(DS3231_REG_SECONDS, date_time, 7);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write time to DS3231: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Wrote time to RTC: %04d-%02d-%02d %02d:%02d:%02d",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

    return ESP_OK;
}

/**
 * @brief write system time to RTC
 */
esp_err_t rtc_set_rtc_time(void)
{
    if (dev_handle == NULL) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Get current system time
    time_t now = time(NULL);
    
    // Write to RTC
    esp_err_t err = rtc_write_time(&now);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write synced time to RTC");
        return err;
    }

    ESP_LOGI(TAG, "Successfully synced time from SYSTEM to RTC");
    return ESP_OK;
}

/**
 * @brief Get current time from RTC and set system time
 */
esp_err_t rtc_set_system_time(void)
{
    if (dev_handle == NULL) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    time_t rtc_time;
    esp_err_t err = rtc_read_time(&rtc_time);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time from RTC");
        return err;
    }

    // Set system time
    struct timeval tv = {
        .tv_sec = rtc_time,
        .tv_usec = 0,
    };

    err = settimeofday(&tv, NULL);
    if (err != 0) {
        ESP_LOGE(TAG, "Failed to set system time");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "System time set from RTC");
    return ESP_OK;
}
