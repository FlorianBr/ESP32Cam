/**
 ******************************************************************************
 *  file           : mqtt.c
 *  brief          : MQTT Connectivity, based on espressifs example
 ******************************************************************************
 *  Copyright (C) 2024 Florian Brandner
 */

/* Includes ------------------------------------------------------------------*/
#include "mqtt.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

#define MAX_URLLEN 64            // Max length of broker URL
#define NVS_NAMESPACE "SETTINGS" // Namespace for the Settings
#define MQTT_ID "ESP32CAM"       // Start of the base ID
#define MAX_RXMSG 10             // Number of received messages

/* Private macro -------------------------------------------------------------*/

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/* Private variables ---------------------------------------------------------*/

static const char *TAG = "MQTT";
static esp_mqtt_client_handle_t client = NULL;
static bool isConnected = false;
static char BaseTopic[MAX_BASE_LENGTH];
static QueueHandle_t xRxQueue = NULL;

/* Private function prototypes -----------------------------------------------*/

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data);

/* Private user code ---------------------------------------------------------*/

/**
 * @brief Event handler registered to receive MQTT events
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base,
           event_id);
  esp_mqtt_event_handle_t event = event_data;
  esp_mqtt_client_handle_t client = event->client;
  int msg_id;
  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGD(TAG, "MQTT_EVENT_CONNECTED");
    isConnected = true;
    break;
  case MQTT_EVENT_DISCONNECTED:
    isConnected = false;
    ESP_LOGD(TAG, "MQTT_EVENT_DISCONNECTED");
    break;
  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGD(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
    break;
  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGD(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_PUBLISHED:
    ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_DATA:
    MQTT_RXMessage RxMsg;
    char cBuffer[MAX_TOPIC_LEN];

    ESP_LOGD(TAG, "MQTT_EVENT_DATA");

    // Queue full? Remove element
    if (uxQueueSpacesAvailable(xRxQueue) == 0) {
      ESP_LOGW(TAG, "RX queue full, removing element!");
      xQueueReceive(xRxQueue, &RxMsg, 0);
    }

    const size_t BaseTopic_len = strlen(BaseTopic);

    if ((BaseTopic_len > 0) && (BaseTopic_len < event->topic_len)) {
      // Copy everything after basetopic/
      memset(&cBuffer[0], 0x00, MAX_TOPIC_LEN);
      memcpy(&cBuffer[0], event->topic + BaseTopic_len + 1,
             event->topic_len - BaseTopic_len - 1);

      // Copy into struct
      memset(&RxMsg, 0x00, sizeof(RxMsg));
      memcpy(&RxMsg.SubTopic[0], &cBuffer[0],
             MIN((MAX_TOPIC_LEN - MAX_BASE_LENGTH), strlen(cBuffer)));
      memcpy(&RxMsg.Payload, event->data, MIN(event->data_len, (MAX_PAYLOAD)));

      ESP_LOGD(TAG, "Enqueueing Rx message: Topic='%s' with %d bytes data",
               RxMsg.SubTopic, strlen(RxMsg.Payload));

      if (!xQueueSend(xRxQueue, &RxMsg, 0)) {
        ESP_LOGW(TAG, "Failed to enqueue Rx message!");
      }
    } else {
      ESP_LOGE(TAG, "Cannot extract subtopic from '%.*s', BL=%d!",
               event->topic_len, event->topic, BaseTopic_len);
    }
    break;
  case MQTT_EVENT_BEFORE_CONNECT:
    ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
    break;
  default:
    ESP_LOGI(TAG, "Other event id:%d", event->event_id);
    break;
  }
} // mqtt_event_handler

/* Public user code ----------------------------------------------------------*/

esp_err_t MQTT_Init(void) {
  size_t url_length = MAX_URLLEN;
  esp_err_t ret = ESP_OK;
  nvs_handle_t handle;
  char cBuffer[MAX_URLLEN];
  uint8_t Mac[6];

  // Read in broker URL
  ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  ESP_ERROR_CHECK(ret);
  ret = nvs_get_str(handle, "MQTT_URL", &cBuffer[0], &url_length);
  ESP_ERROR_CHECK(ret);
  nvs_close(handle);
  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = cBuffer,
  };
  ESP_LOGI(TAG, "Broker address is: %s", mqtt_cfg.broker.address.uri);

  // Setup MQTT client
  client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                 NULL);
  esp_mqtt_client_start(client);

  // Generate base topic with id and mac address
  ESP_ERROR_CHECK(esp_efuse_mac_get_default(&Mac[0]));
  snprintf(&BaseTopic[0], MAX_BASE_LENGTH, "%s_%02x%02x%02x%02x%02x%02x",
           MQTT_ID, Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
  ESP_LOGI(TAG, "Basetopic is '%s'", BaseTopic);

  // Create queue for received data
  xRxQueue = xQueueCreate(MAX_RXMSG, sizeof(MQTT_RXMessage));
  if (NULL == xRxQueue) {
    ESP_LOGE(TAG, "Failed to create RX queue!");
  }

  return (ESP_OK);
}

esp_err_t MQTT_Transmit(const char *SubTopic, const char *Payload) {
  char FullTopic[MAX_TOPIC_LEN];

  if (!isConnected) {
    ESP_LOGW(TAG, "Cannot transmit: Not connected");
    return (ESP_FAIL);
  }

  // generate full topic: Bae Topic / Subtopic(s)
  snprintf(&FullTopic[0], MAX_TOPIC_LEN, "%s/%s", BaseTopic, SubTopic);

  // Transmit, QoS always 1 and no retaining
  int msg_id = esp_mqtt_client_publish(client, FullTopic, Payload, 0, 1, 0);

  if (0 > msg_id) {
    ESP_LOGW(TAG, "Cannot transmit: Code %d", msg_id);
    return (ESP_FAIL);
  }
  return (ESP_OK);
}

esp_err_t MQTT_Subscribe(const char *SubTopic) {
  char FullTopic[MAX_TOPIC_LEN];

  snprintf(&FullTopic[0], MAX_TOPIC_LEN, "%s/%s", BaseTopic, SubTopic);

  int msg_id = esp_mqtt_client_subscribe(client, FullTopic, 0);

  if (0 > msg_id) {
    ESP_LOGW(TAG, "Cannot subscribe: Code %d", msg_id);
    return (ESP_FAIL);
  }
  ESP_LOGD(TAG, "Subscribe successful, msg_id=%d", msg_id);
  return (ESP_OK);
}

esp_err_t MQTT_Unsubscribe(const char *SubTopic) {
  char FullTopic[MAX_TOPIC_LEN];

  snprintf(&FullTopic[0], MAX_TOPIC_LEN, "%s/%s", BaseTopic, SubTopic);

  int msg_id = esp_mqtt_client_unsubscribe(client, FullTopic);

  if (0 > msg_id) {
    ESP_LOGW(TAG, "Cannot unsubscribe: Code %d", msg_id);
    return (ESP_FAIL);
  }
  ESP_LOGI(TAG, "Unsubscribe successful, msg_id=%d", msg_id);
  return (ESP_OK);
}

QueueHandle_t MQTT_GetRxQueue() { return (xRxQueue); }

bool MQTT_isConnected() { return isConnected; }