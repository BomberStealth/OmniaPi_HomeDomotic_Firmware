/**
 * OmniaPi Gateway - BLE WiFi Provisioning
 *
 * Activates BLE provisioning when:
 * - No Ethernet connected AND no WiFi credentials in NVS
 * - WiFi credentials exist but connection fails after retries
 *
 * BLE device name: "OmniaPi-XXXX" (last 4 hex of MAC)
 * PoP: "omniapi"
 */

#ifndef BLE_PROV_H
#define BLE_PROV_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize and start BLE WiFi provisioning.
 * Blocks until credentials are received, then saves to NVS and reboots.
 * @return ESP_OK on success (never returns on success - reboots)
 */
esp_err_t ble_prov_start(void);

/**
 * Check if BLE provisioning should be activated.
 * @param eth_connected  true if Ethernet link is up
 * @return true if BLE provisioning is needed
 */
bool ble_prov_needed(bool eth_connected);

#ifdef __cplusplus
}
#endif

#endif // BLE_PROV_H
