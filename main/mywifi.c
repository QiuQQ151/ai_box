// freertos
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
// esp_system
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "nvs_flash.h"
//
#include "periph_wifi.h"
//
#include "sysinfo.h"
#include "usersntp.h"
#include "mywifi.h"

extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例

static const char *TAG_wifi = "wifi_station";

void wifi_init_task(void *arg)
{

    ESP_LOGI(TAG_wifi, "use ADF to init wifi");
    // 使用ADF的Wi-Fi外设初始化（保持原有代码）
    periph_wifi_cfg_t wifi_cfg = {
        .wifi_config.sta.ssid = WIFI_SSID,
        .wifi_config.sta.password = WIFI_PASS,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    while (1)
    {
        // 等待nvs初始化完成
        uint32_t notify_value;
        if (xTaskNotifyWait(0, 0, &notify_value, pdMS_TO_TICKS(portMAX_DELAY)) == pdTRUE)
        {
            ESP_LOGI(TAG_wifi, "NVS init done. Value: %lu", notify_value);

            // 安全拷贝 SSID
            strncpy(wifi_cfg.wifi_config.sta.ssid, sys_info.ssid, sizeof(wifi_cfg.wifi_config.sta.ssid) - 1);
            wifi_cfg.wifi_config.sta.ssid[sizeof(wifi_cfg.wifi_config.sta.ssid) - 1] = '\0';
            // 安全拷贝 password
            strncpy(wifi_cfg.wifi_config.sta.password, sys_info.password, sizeof(wifi_cfg.wifi_config.sta.password) - 1);
            wifi_cfg.wifi_config.sta.password[sizeof(wifi_cfg.wifi_config.sta.password) - 1] = '\0';

            esp_periph_start(sys_resource.set, wifi_handle);
            periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);
            ESP_LOGI(TAG_wifi, "init done");

            // 通知lvgl的任务
            TaskHandle_t task_handle = (TaskHandle_t)arg;
            xTaskNotify(task_handle, 2, eSetValueWithOverwrite); // 通知值2

            break;
        }
        else
        {
            ESP_LOGE(TAG_wifi, "NVS init timeout!");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    while (1)
    {
        sys_info.wifi_connected = periph_wifi_is_connected(wifi_handle);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


// 更新WiFi信息
void update_wifi_info(void)
{
 
}
