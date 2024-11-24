/**
 ******************************************************************************
 *  file           : main.c
 *  brief          : ESP32 Cam Main
 ******************************************************************************
 *  Copyright (C) 2024 Florian Brandner
 */

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Private includes ----------------------------------------------------------*/
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "mqtt.h"
#include "nvs_flash.h"
#include "wifi.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static const char *TAG = "MAIN";

/* Private function prototypes -----------------------------------------------*/

/* Private user code ---------------------------------------------------------*/

/* Public user code ----------------------------------------------------------*/

void app_main(void) {
  esp_err_t ret = ESP_OK;

  // Get chip info
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  // Core info
  ESP_LOGW(TAG, "-------------------------------------");
  ESP_LOGW(TAG, "System Info:");
  ESP_LOGW(TAG, "%s chip with %d CPU cores, WiFi%s%s%s%s%s%s, ",
           CONFIG_IDF_TARGET, chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "/FLASH" : "",
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "/WiFi" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? "/WPAN" : "",
           (chip_info.features & CHIP_FEATURE_EMB_PSRAM) ? "/PSRAM" : "");
  ESP_LOGW(TAG, "Heap: %lu", esp_get_free_heap_size());
  ESP_LOGW(TAG, "Reset reason: %d", esp_reset_reason());
  ESP_LOGW(TAG, "-------------------------------------");

  // Initialize NVS, format it if necessary
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "Erasing NVS!");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_LOGI(TAG, "NVS init returned %d", ret);

  // Print out NVS statistics
  nvs_stats_t nvs_stats;
  nvs_get_stats("nvs", &nvs_stats);
  ESP_LOGW(TAG, "-------------------------------------");
  ESP_LOGW(TAG, "NVS Statistics:");
  ESP_LOGW(TAG, "NVS Used = %d", nvs_stats.used_entries);
  ESP_LOGW(TAG, "NVS Free = %d", nvs_stats.free_entries);
  ESP_LOGW(TAG, "NVS All = %d", nvs_stats.total_entries);

  nvs_iterator_t iter = NULL;
  esp_err_t res = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &iter);
  while (res == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(iter, &info);
    ESP_LOGW(TAG, "Key '%s', Type '%d'", info.key, info.type);
    res = nvs_entry_next(&iter);
  }
  nvs_release_iterator(iter);
  ESP_LOGW(TAG, "-------------------------------------");

#if 0 // Set NVS values here
  {
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("SETTINGS", NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_str(handle, "WIFI_SSID", "<Name>"));
    ESP_ERROR_CHECK(nvs_set_str(handle, "WIFI_PASS", "<Secret>"));
    ESP_ERROR_CHECK(nvs_set_str(handle, "MQTT_URL", "mqtt://<Address>:1883"));
    nvs_close(handle);
  }
#endif

  if (ESP_OK == WiFi_Init()) {
    MQTT_Init();
  } else {
    // TODO: If WiFi init failed, re-init in AP mode
  }

  ESP_LOGW(TAG, "Entering loop");
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(30000));
    ESP_LOGW(TAG, ".");
  }
}
