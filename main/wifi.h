/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi / DPP enrollee status
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,   /**< WiFi disconnected, DPP not active */
    WIFI_STATUS_DPP_LISTENING,      /**< DPP enrollee listening for authentication */
    WIFI_STATUS_DPP_READY,          /**< DPP QR code / bootstrap ready, waiting for scan */
    WIFI_STATUS_CONNECTING,         /**< WiFi credentials received, connecting to AP */
    WIFI_STATUS_CONNECTED,          /**< WiFi connected and IP obtained */
    WIFI_STATUS_DPP_FAILED,         /**< DPP authentication failed after retries */
} wifi_status_t;

/**
 * @brief Initialize WiFi in STA mode with DPP enrollee support.
 *
 * This function sets up the WiFi driver, initializes the DPP supplicant,
 * generates a bootstrap (QR code URI), and starts listening for DPP
 * authentication. The QR code URI can be retrieved with wifi_get_dpp_uri().
 *
 * The function is non-blocking; connection events are handled asynchronously
 * in event handlers.
 *
 * @return ESP_OK on success, appropriate error code otherwise.
 */
esp_err_t wifi_init(void);

/**
 * @brief Check if WiFi is currently connected (IP obtained).
 *
 * @return true if connected, false otherwise.
 */
bool wifi_is_connected(void);

/**
 * @brief Get the current WiFi/DPP status.
 */
wifi_status_t wifi_get_status(void);

/**
 * @brief Get the DPP bootstrap URI (QR code content).
 *
 * @param buf  Destination buffer for the URI string.
 * @param len  Size of the destination buffer.
 * @return ESP_OK if URI is available, ESP_ERR_NOT_FOUND if not yet ready.
 */
esp_err_t wifi_get_dpp_uri(char *buf, size_t len);

/**
 * @brief Get the SSID the device is connected to.
 *
 * @param buf  Destination buffer for the SSID.
 * @param len  Size of the destination buffer.
 * @return ESP_OK if connected and SSID available, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t wifi_get_ssid(char *buf, size_t len);

/**
 * @brief Get the IP address assigned to the WiFi station interface.
 *
 * @param buf  Destination buffer for the IP address string.
 * @param len  Size of the destination buffer.
 * @return ESP_OK if IP is available, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t wifi_get_ip(char *buf, size_t len);

/**
 * @brief Trigger DPP bootstrap regeneration.
 *
 * If the device is in DPP listening mode, this regenerates the bootstrap
 * key and QR code URI. The new URI can then be retrieved with
 * wifi_get_dpp_uri().
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t dpp_trigger_bootstrap(void);
void wifi_broadcast_state(void);

#ifdef __cplusplus
}
#endif
