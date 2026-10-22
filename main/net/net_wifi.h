/*
 * net_wifi.h — Joining the user's Wi-Fi network.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Wi-Fi is optional. Without it the buddy still shows its face, reads its
 * sensors, plays the music on its flash and answers every offline phrase — it
 * simply cannot answer open questions or know the time. So nothing here is
 * allowed to be fatal, and every failure ends in a message the user can act on.
 *
 * Credentials are typed on the touchscreen and kept in the settings, which
 * avoids dragging in a whole provisioning stack for a single desk toy.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Where the connection currently stands.
 */
typedef enum {
    NET_WIFI_IDLE = 0,      /*!< No credentials, or not asked to connect yet   */
    NET_WIFI_CONNECTING,
    NET_WIFI_CONNECTED,
    NET_WIFI_FAILED,        /*!< Gave up; the reason is in net_wifi_get_error()*/
} net_wifi_state_t;

/**
 * @brief One network found by a scan.
 */
typedef struct {
    char    ssid[33];
    int8_t  signal_strength;  /*!< dBm; closer to zero is stronger             */
    bool    needs_password;
} net_wifi_network_t;

/**
 * @brief Called whenever the connection state changes.
 *
 * @warning Runs on the system event task. Take the display lock before drawing.
 */
typedef void (*net_wifi_state_cb_t)(net_wifi_state_t state);

/**
 * @brief Called when an asynchronous scan finishes.
 *
 * @param networks  Array of results, strongest first. Valid only for the call.
 * @param count     How many results there are.
 */
typedef void (*net_wifi_scan_cb_t)(const net_wifi_network_t *networks, size_t count);

/**
 * @brief Bring up the network stack and the Wi-Fi driver.
 *
 * Does not connect to anything.
 */
esp_err_t net_wifi_init(void);

/**
 * @brief Start connecting using the credentials in the settings.
 *
 * Returns immediately; watch the state callback for the outcome.
 *
 * @return ESP_ERR_INVALID_STATE when no network has been configured.
 */
esp_err_t net_wifi_connect_using_saved_credentials(void);

/**
 * @brief Save new credentials and connect to them.
 *
 * @param ssid      Network name.
 * @param password  Passphrase; pass "" for an open network.
 */
esp_err_t net_wifi_connect(const char *ssid, const char *password);

/**
 * @brief Disconnect and stop trying to reconnect.
 */
void net_wifi_disconnect(void);

/**
 * @brief True once an IP address has been assigned.
 */
bool net_wifi_is_connected(void);

/**
 * @brief Current connection state.
 */
net_wifi_state_t net_wifi_get_state(void);

/**
 * @brief The IP address as text, or an empty string when not connected.
 */
const char *net_wifi_get_ip_address(void);

/**
 * @brief The name of the network being used, or an empty string.
 */
const char *net_wifi_get_ssid(void);

/**
 * @brief A short description of why the last attempt failed.
 */
const char *net_wifi_get_error(void);

/**
 * @brief True when the radio link is up, whether or not an address arrived.
 *
 * The two halves of joining a network fail for completely different reasons: a
 * refused association means the password or the band is wrong, while an
 * association with no address means the router accepted us and then never
 * finished DHCP. This is what lets the screen say which.
 */
bool net_wifi_is_associated(void);

/**
 * @brief Register a listener for connection state changes.
 */
void net_wifi_set_state_callback(net_wifi_state_cb_t callback);

/**
 * @brief Scan for nearby networks without blocking the caller.
 *
 * A scan takes a couple of seconds, which would freeze the face if it ran on
 * the drawing task, so it runs on a short-lived task of its own.
 *
 * @param callback  Called with the results when the scan finishes.
 */
esp_err_t net_wifi_scan_async(net_wifi_scan_cb_t callback);

#ifdef __cplusplus
}
#endif
