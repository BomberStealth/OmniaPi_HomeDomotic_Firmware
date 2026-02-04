/**
 * OmniaPi Gateway Mesh - Ethernet Manager
 * WT32-ETH01 with LAN8720 PHY
 */

#ifndef ETH_MANAGER_H
#define ETH_MANAGER_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t eth_manager_init(void);
esp_err_t eth_manager_start(void);
esp_err_t eth_manager_stop(void);
bool eth_manager_is_connected(void);
void eth_manager_get_ip(char *ip_str, size_t len);

/**
 * Get Ethernet network interface (for route priority management)
 */
esp_netif_t *eth_manager_get_netif(void);

/**
 * Get init error description (NULL if init succeeded)
 */
const char *eth_manager_get_init_error(void);

#ifdef __cplusplus
}
#endif

#endif // ETH_MANAGER_H
