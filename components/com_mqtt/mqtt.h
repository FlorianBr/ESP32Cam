/**
 ******************************************************************************
 *  file           : mqtt.h
 *  brief          : MQTT Connectivity
 ******************************************************************************
 *  Copyright (C) 2024 Florian Brandner
 */

#ifndef MQTT_H_
#define MQTT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Private includes ----------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

#define MAX_TOPIC_LEN 250   // Max length of full topic
#define MAX_BASE_LENGTH 128 // Max length base topic
#define MAX_PAYLOAD 128     // Max size of payload

/* Exported types ------------------------------------------------------------*/

typedef struct MQTT_RXMessage {
  char SubTopic[MAX_TOPIC_LEN - MAX_BASE_LENGTH];
  char Payload[MAX_PAYLOAD];
} MQTT_RXMessage;

/* Exported macro ------------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

/**
 * @brief Init MQTT
 *
 * @return esp_err_t
 */
esp_err_t MQTT_Init(void);

/**
 * @brief Transmit Data to MQTT
 *
 * @param SubTopic The subtopic to send to
 * @param Payload The payload to send
 * @return esp_err_t
 */
esp_err_t MQTT_Transmit(const char* SubTopic, const char* Payload);

/**
 * @brief Subscribe to a subtopic
 *
 * @param SubTopic
 * @return esp_err_t
 */
esp_err_t MQTT_Subscribe(const char* SubTopic);

/**
 * @brief Unsubscribe from a topic
 *
 * @param SubTopic
 * @return esp_err_t
 */
esp_err_t MQTT_Unsubscribe(const char* SubTopic);

/**
 * @brief Get the handle to the RX queue
 *
 * @return QueueHandle_t
 */
QueueHandle_t MQTT_GetRxQueue();

/**
 * @brief Check if MQTT is connected
 *
 * @return true if connected to the broker, false otherwise
 */
bool MQTT_isConnected();

#ifdef __cplusplus
}
#endif

#endif // MQTT_H_
