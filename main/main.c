/**
 ******************************************************************************
 *  file           : main.c
 *  brief          : ESP32 Cam Main
 ******************************************************************************
 *  Copyright (C) 2024 Florian Brandner
 */

/* Includes ------------------------------------------------------------------*/
#include <cJSON.h>
#include <esp_camera.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Private includes ----------------------------------------------------------*/
#include "esp_camera.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "mqtt.h"
#include "nvs_flash.h"
#include "wifi.h"

/* Private typedef -----------------------------------------------------------*/

typedef struct {
   httpd_req_t *req;
   size_t len;
} jpg_chunking_t;

/* Private define ------------------------------------------------------------*/

#define CAM_PIN_RESET 5
#define CAM_PIN_XCLK  15
#define CAM_PIN_SIOD  22
#define CAM_PIN_SIOC  23
#define CAM_PIN_D7    39
#define CAM_PIN_D6    34
#define CAM_PIN_D5    33
#define CAM_PIN_D4    27
#define CAM_PIN_D3    12
#define CAM_PIN_D2    35
#define CAM_PIN_D1    14
#define CAM_PIN_D0    2
#define CAM_PIN_VSYNC 18
#define CAM_PIN_HREF  36
#define CAM_PIN_PCLK  26

#define PART_BOUNDARY "123456789000000000000987654321"

#define TIME_MQTT_STATS (30 * 1000)   // [ms] Cycle time for MQTT stats
#define TIME_MQTT_IMAGE (60 * 1000)   // [ms] Cycle time for MQTT image

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

static const char *TAG = "MAIN";

static camera_config_t camera_config = {
    .pin_pwdn = -1,   // Unused
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,
    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,
    .xclk_freq_hz = 16000000,   // 16 MHz
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_SXGA,
    .jpeg_quality = 5,
    .fb_count = 1,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

/* Private function prototypes -----------------------------------------------*/

static void task_mqtt_stats(void *pvParameters);
static void task_mqtt_image(void *pvParameters);
static size_t encode_jpg_stream(void *arg, size_t index, const void *data, size_t len);
static esp_err_t stream_handler(httpd_req_t *req);
static esp_err_t snapshot_handler(httpd_req_t *req);
static httpd_handle_t start_webserver();

/* Private user code ---------------------------------------------------------*/

// The available URIs
static const httpd_uri_t uri_snapshot = {
    .uri = "/snapshot",
    .method = HTTP_GET,
    .handler = snapshot_handler,
};
static const httpd_uri_t uri_stream = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
};

/**
 * @brief  Callback to encode the JPEG stream
 */
static size_t encode_jpg_stream(void *arg, size_t index, const void *data, size_t len) {
   jpg_chunking_t *j = (jpg_chunking_t *)arg;
   if (!index) {
      j->len = 0;
   }
   if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
      return 0;
   }
   j->len += len;
   return len;
}

/**
 * @brief HTTP Request: Nahdle a JPG Snapshot
 */
static esp_err_t snapshot_handler(httpd_req_t *req) {
   camera_fb_t *fb = NULL;
   esp_err_t res = ESP_OK;

   fb = esp_camera_fb_get();
   if (!fb) {
      ESP_LOGE(TAG, "SNAPSHOT: Camera capture failed");
      httpd_resp_send_500(req);
      return ESP_FAIL;
   }
   res = httpd_resp_set_type(req, "image/jpeg");
   if (res == ESP_OK) {
      res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
   }

   if (res == ESP_OK) {
      if (fb->format == PIXFORMAT_JPEG) {
         res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
      } else {
         jpg_chunking_t jchunk = {req, 0};
         res = frame2jpg_cb(fb, 80, encode_jpg_stream, &jchunk) ? ESP_OK : ESP_FAIL;
         httpd_resp_send_chunk(req, NULL, 0);
      }
   }
   esp_camera_fb_return(fb);
   return res;
}

/**
 * @brief HTTP Request: Handle a JPG Stream
 */
static esp_err_t stream_handler(httpd_req_t *req) {
   camera_fb_t *fb = NULL;
   esp_err_t res = ESP_OK;
   size_t _jpg_buf_len;
   uint8_t *_jpg_buf;
   char *part_buf[64];

   res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
   if (res != ESP_OK) {
      return res;
   }

   while (true) {
      fb = esp_camera_fb_get();
      if (!fb) {
         ESP_LOGE(TAG, "STREAM: Camera capture failed");
         res = ESP_FAIL;
         break;
      }
      if (fb->format != PIXFORMAT_JPEG) {
         bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
         if (!jpeg_converted) {
            ESP_LOGE(TAG, "STREAM: JPEG compression failed");
            esp_camera_fb_return(fb);
            res = ESP_FAIL;
         }
      } else {
         _jpg_buf_len = fb->len;
         _jpg_buf = fb->buf;
      }

      if (res == ESP_OK) {
         res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
      }
      if (res == ESP_OK) {
         size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);

         res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
      }
      if (res == ESP_OK) {
         res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
      }
      if (fb->format != PIXFORMAT_JPEG) {
         free(_jpg_buf);
      }
      esp_camera_fb_return(fb);
      if (res != ESP_OK) {
         break;
      }
   }

   return res;
}

/**
 * @brief Start the webserver and register URIs
 */
static httpd_handle_t start_webserver(void) {
   httpd_handle_t server = NULL;
   httpd_config_t config = HTTPD_DEFAULT_CONFIG();

   config.lru_purge_enable = true;

   ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
   if (httpd_start(&server, &config) == ESP_OK) {
      // Set URI handlers
      httpd_register_uri_handler(server, &uri_snapshot);
      httpd_register_uri_handler(server, &uri_stream);

      return server;
   }

   ESP_LOGE(TAG, "Error starting webserver!");
   return NULL;
}

/**
 * @brief Task: Send cyclic MQTT statistics
 */
static void task_mqtt_stats(void *pvParameters) {
   while (1) {
      vTaskDelay(pdMS_TO_TICKS(TIME_MQTT_STATS));

      cJSON *jsonMsg = cJSON_CreateObject();
      if (NULL != jsonMsg) {
         char *string = NULL;
         time_t now;
         time(&now);
         cJSON_AddNumberToObject(jsonMsg, "Uptime", (xTaskGetTickCount() * configTICK_RATE_HZ) / 10000);
         cJSON_AddNumberToObject(jsonMsg, "Timestamp", now);
         cJSON_AddStringToObject(jsonMsg, "Firmware", __TIME__ " " __DATE__);

         string = cJSON_Print(jsonMsg);
         MQTT_Transmit("Status", string, 0);

         if (NULL != jsonMsg) {
            cJSON_Delete(jsonMsg);
         }
         if (NULL != string) {
            free(string);
         }
      }
   }
}

// Task: Send MQTT Image
static void task_mqtt_image(void *pvParameters) {
   while (1) {
      vTaskDelay(pdMS_TO_TICKS(TIME_MQTT_IMAGE));

      // TX a JPG to MQTT
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) {
         if (fb->format == PIXFORMAT_JPEG) {
            MQTT_Transmit("Snapshot", (const char *)fb->buf, fb->len);
         } else {
            ESP_LOGW(TAG, "Framebuffer format 0x%02x not supported!", fb->format);
         }
         esp_camera_fb_return(fb);
      }
   }
}

/* Public user code ----------------------------------------------------------*/

void app_main(void) {
   esp_err_t ret = ESP_OK;

   // Get chip info
   esp_chip_info_t chip_info;
   esp_chip_info(&chip_info);

   // Core info
   ESP_LOGW(TAG, "-------------------------------------");
   ESP_LOGW(TAG, "System Info:");
   ESP_LOGW(TAG, "%s chip with %d CPU cores, WiFi%s%s%s%s%s%s, ", CONFIG_IDF_TARGET, chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "", (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "/FLASH" : "",
            (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "/WiFi" : "",
            (chip_info.features & CHIP_FEATURE_IEEE802154) ? "/WPAN" : "",
            (chip_info.features & CHIP_FEATURE_EMB_PSRAM) ? "/PSRAM" : "");
   ESP_LOGW(TAG, "Heap: %lu", esp_get_free_heap_size());
   ESP_LOGW(TAG, "Reset reason: %d", esp_reset_reason());
   ESP_LOGW(TAG, "-------------------------------------");

   // Initialize NVS, format it if necessary
   ret = nvs_flash_init();
   if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
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

#if 0   // Set NVS values here
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
      start_webserver();
   } else {
      // TODO: If WiFi init failed, re-init in AP mode
   }

   ESP_ERROR_CHECK(esp_camera_init(&camera_config));

   ESP_LOGI(TAG, "Entering loop");

   // Start the tasks
   xTaskCreate(task_mqtt_image, "MQTT Image", 4096, NULL, tskIDLE_PRIORITY, NULL);
   xTaskCreate(task_mqtt_stats, "MQTT Stats", 4096, NULL, tskIDLE_PRIORITY, NULL);

   while (1) {
      vTaskDelay(pdMS_TO_TICKS(10000));
      ESP_LOGI(TAG, ".");
   }
}
