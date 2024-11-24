/**
 ******************************************************************************
 *  file           : wifi.h
 *  brief          : WiFi Functions
 ******************************************************************************
 */

#ifndef _WIFI_H_
#define _WIFI_H_

#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

/* Private includes ----------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief Init WiFi in station mode
 *
 * @return esp_err_t
 */
esp_err_t WiFi_Init(void);

/**
 * @brief Get the network interface
 *
 * @return esp_netif_t*
 */
esp_netif_t* WiFi_GetNetIf();

/**
 * @brief Check if WiFi is connected
 *
 * @return true if connected, false otherwise
 */
bool WiFi_isConnected();

#ifdef __cplusplus
}
#endif

#endif // _WIFI_H_