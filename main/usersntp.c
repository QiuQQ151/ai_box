#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
// freertos
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
// esp_system
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
//
//
#include "sysinfo.h"

extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例

static const char *TAG_sntp = "sntp_station";
static int s_retry_num = 0;

void sntp_update_time(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    // 只更新时间部分（HH:MM:SS）
    strftime(sys_info.current_time, sizeof(sys_info.current_time), "%H:%M:%S", &timeinfo);
    vTaskDelay(pdMS_TO_TICKS(100)); // 每秒一次
}
void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG_sntp, "Time synchronized!");
}

void sntp_init_task(void)
{
    while (!sys_info.wifi_connected)
    {
        ESP_LOGI(TAG_sntp, "Waiting for wifi init...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG_sntp, "Initializing SNTP");
    // 设置时间同步回调
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    // 配置SNTP服务器
    esp_sntp_setservername(0, PRIMARY_SNTP_SERVER);
    esp_sntp_setservername(1, SECONDARY_SNTP_SERVER);
    // 设置SNTP操作模式
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    // 初始化SNTP服务
    esp_sntp_init();
    setenv("TZ", "CST-8", 1); // 或 "UTC-8"
    tzset();                  // 生效时区配置
    // 等待时间同步
    ESP_LOGI(TAG_sntp, "Waiting for time synchronization...");
    int retry = 0;
    const int retry_count = 15;
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED &&
           ++retry < retry_count)
    {
        ESP_LOGI(TAG_sntp, "Waiting... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    if (retry == retry_count)
    {
        ESP_LOGE(TAG_sntp, "Failed to synchronize time!");
    }
    else
    {
        ESP_LOGI(TAG_sntp, "Time synchronized successfully");
    }
    while (1)
    {
        sntp_update_time();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void print_local_time(void)
{
    setenv("TZ", "CST-8", 1); // 或 "UTC-8"
    tzset();                  // 生效时区配置

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG_sntp, "Current local time: %s", strftime_buf);
}