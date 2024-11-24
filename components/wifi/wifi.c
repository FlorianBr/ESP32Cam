/**
 ******************************************************************************
 *  file           : wifi.c
 *  brief          : WiFi Functions, based on espressifs WiFi station example
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>

/* Private includes ----------------------------------------------------------*/

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

#define WIFI_CONNECTED_BIT BIT0  // Event: Connected
#define WIFI_FAIL_BIT BIT1       // Event: Fail
#define WIFI_MAXIMUM_RETRY 10    // Max number of retries
#define MAX_SSIDLEN 128          // Max length of the SSID String
#define MAX_PASSLEN 128          // Max length of the Password String
#define NVS_NAMESPACE "SETTINGS" // Namespace for the Settings

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

static const char* TAG = "WIFI";
static EventGroupHandle_t s_wifi_event_group; // FreeRTOS event group to signal when we are connected
static int s_retry_num = 0;                   // Reconnect counter
static esp_netif_t* wifi_NetIf;               // The network interface
static bool isConnected = false;              // Current connection state

/* Private function prototypes -----------------------------------------------*/

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

/* Private user code ---------------------------------------------------------*/

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
    isConnected = false;
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < WIFI_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "connect to the AP fail");
    isConnected = false;
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    isConnected = true;
  }
}

/* Public user code ----------------------------------------------------------*/

esp_err_t WiFi_Init(void) {
  size_t ssid_length     = MAX_SSIDLEN;
  size_t password_length = MAX_PASSLEN;
  nvs_handle_t handle;

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid               = "",
              .password           = "",
              .threshold.authmode = WIFI_AUTH_OPEN,
              .sae_pwe_h2e        = WPA3_SAE_PWE_BOTH,
          },
  };

  // Read in settings from NVS
  if (ESP_OK != nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle)) {
    ESP_LOGE(TAG, "FAILED to open nvs");
    return ESP_FAIL;
  }

  if (ESP_OK != nvs_get_str(handle, "WIFI_SSID", (char*)&wifi_config.sta.ssid[0], &ssid_length)) {
    ESP_LOGE(TAG, "FAILED to read SSID from nvs");
    nvs_close(handle);
    return ESP_FAIL;
  }

  if (ESP_OK != nvs_get_str(handle, "WIFI_PASS", (char*)&wifi_config.sta.password[0], &password_length)) {
    ESP_LOGE(TAG, "FAILED to read pass from nvs");
    nvs_close(handle);
    return ESP_FAIL;
  }

  nvs_close(handle);

  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifi_NetIf = esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(
      esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(
      esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "Init finished");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT)
     or connection failed for the maximum number of re-tries (WIFI_FAIL_BIT).
     The bits are set by event_handler() (see above) */
  EventBits_t bits =
      xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually happened.
   */
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Connected to AP, SSID: %s", wifi_config.sta.ssid);
    isConnected = true;
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID: %s", wifi_config.sta.ssid);
    isConnected = false;
  } else {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
    isConnected = false;
  }

  /* The event will not be processed after unregister */
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
  vEventGroupDelete(s_wifi_event_group);

  return (ESP_OK);
} // wifi_init

esp_netif_t* WiFi_GetNetIf() {
  return (wifi_NetIf);
}

bool WiFi_isConnected() {
  return (isConnected);
}