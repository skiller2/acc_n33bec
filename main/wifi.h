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
    WIFI_STATUS_AP_ACTIVE,          /**< WiFi AP mode is active (e.g. provisioning) */
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
 * @brief Stop the WiFi driver and DPP enrollee.
 *
 * Used to disable WiFi when an alternative interface (e.g. Ethernet) is
 * preferred.
 */
void wifi_stop(void);

/**
 * @brief Request that WiFi be disabled because Ethernet already has an IP.
 *
 * If WiFi has not been initialised yet, this sets a flag so that the next
 * wifi_init() will return without bringing WiFi up. If WiFi is already
 * running, this stops it immediately.
 */
void wifi_request_stop_by_ethernet(void);

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

/**
 * @brief Start a WiFi access point using the device hostname as SSID.
 *
 * Stops the STA interface and brings up an AP with:
 *   SSID     = esp_netif_get_hostname() (falls back to "esp_ap" if unavailable)
 *   password = "12345678"
 *   auth     = WPA2_PSK
 *   channel  = 1, max 4 clients
 *
 * Intended to be called from a factory-reset / boot-button handler.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t wifi_start_ap_from_hostname(void);

#ifdef __cplusplus
}
#endif
