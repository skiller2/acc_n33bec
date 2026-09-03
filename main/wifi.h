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

/**
 * @brief Get the current WiFi/DPP status.
 */

/**
 * @brief Get the DPP bootstrap URI (QR code content).
 *
 * @param buf  Destination buffer for the URI string.
 * @param len  Size of the destination buffer.
 * @return ESP_OK if URI is available, ESP_ERR_NOT_FOUND if not yet ready.
 */
esp_err_t wifi_get_dpp_uri(char *buf, size_t len);

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
 * @brief Get the currently stored WiFi credentials from NVS.
 *
 * @param ssid_buf      Destination buffer for the SSID.
 * @param ssid_buf_len  Size of the SSID buffer.
 * @param pass_buf      Destination buffer for the password.
 * @param pass_buf_len  Size of the password buffer.
 * @return ESP_OK if credentials are stored, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t wifi_get_credentials(char *ssid_buf, size_t ssid_buf_len,
                               char *pass_buf, size_t pass_buf_len);

/**
 * @brief Clear stored WiFi credentials from NVS.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t wifi_clear_credentials(void);

/**
 * @brief Trigger a WiFi scan and return the list of visible APs as JSON.
 *
 * The returned string is heap-allocated and must be free()d by the caller.
 * On error, returns NULL.
 */
char *wifi_scan_to_json(void);

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
esp_err_t wifi_start_service_mode(void);

esp_err_t wifi_sta_start(void);

esp_err_t dpp_start(void);

esp_err_t wifi_save_credentials(const char *ssid, const char *password);

void wifi_stop(void);

void ws_broadcast_wifi_status_last(void);

#ifdef __cplusplus
}
#endif
