#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_idf_version.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "baidu_access_token.h"
#include "esp_peripherals.h"
#include "periph_button.h"
#include "periph_wifi.h"
#include "esp_netif.h"
#include <string.h>
#include "esp_timer.h"
#include "lvgl.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_vendor.h"

#include "mynvs.h"
#include "mywifi.h"
#include "myes8311.h"
#include "mylvgl.h"
#include "mykey.h"
#include "usersntp.h"
#include "usertask.h"
#include "app.h"
#include "sysinfo.h"

static const char *TAG = "app_main";

// 系统状态信息结构体实例
sys_info_t sys_info = {
    .nvs_stata = false,

    .battery_level = 50,

    .wifi_signal_strength = 4,
    .wifi_connected = false,
    .wifi_ssid = "NULL",
    .wifi_ip = "NULL",
    .wifi_mac = "NULL",

    .bluetooth_connected = false,
    .volume_level = 0,
    .is_muted = false,
    .current_time = "--:--",
    .is_playing = false,
    .unused_mem = 10,
};
// 系统资源接口结构体实例
sys_resource_t sys_resource = {
    .set = NULL,
    .es8311_init_done = NULL,
    .disp = NULL, // lvgl的屏幕句柄
    .key_press_count_queue = NULL,
    .key_to_app_queue = NULL,
    .current_app_id = 0,
};

void my_system_init_task(void *arg)
{
    TaskHandle_t lvgl_init_task_handle;
    // esp8311 和 LCD 共用i2c
    xTaskCreate(lvgl_init_task, "lvgl_init_task", 1024 * 5, NULL, PRI_DISP, &lvgl_init_task_handle);    // lvgl初始化(非一次性任务)
    xTaskCreate(es8311_init_task, "es8311_init_task", 1024 * 4, lvgl_init_task_handle, PRI_COMM, NULL); // 一次性任务

    TaskHandle_t wifi_init_task_handle; // wifi任务句柄
    // wifi 完成后通知lvgl
    xTaskCreate(wifi_init_task, "wifi_init_task", 4096, lvgl_init_task_handle, PRI_COMM, &wifi_init_task_handle); // wifi初始化(后台) (需等待nvs完成)
    // nvs  完成后通知wifi
    xTaskCreate(nvs_init_task, "nvs_init_task", 1024 * 20, wifi_init_task_handle, PRI_COMM, NULL); // 初始化NVS(若配网需要较大的栈空间)
    // sntp时间同步 轻量后台服务
    xTaskCreate(sntp_init_task, "sntp_init_task", 2048, NULL, PRI_SENS, NULL); // sntp(后台)

    // key
    // xTaskCreate(key_init_task, "key_init_task", 1024 * 2, NULL, PRI_COMM, NULL);
    key_init_task(NULL);
    vTaskDelay(pdMS_TO_TICKS(1000));
    xTaskCreate(DBG_task, "DBG_task", 1024 * 4, NULL, PRI_DBG, NULL); // 创建调试任务(后台)
    vTaskDelete(NULL);                                                // 删除任务
}

/**************************** 主函数 **********************************/
void app_main(void)
{
    // 初始化外设集合句柄
    ESP_LOGI(TAG, "Initialize set");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    sys_resource.set = esp_periph_set_init(&periph_cfg);

    xTaskCreate(my_system_init_task, "my_system_init_task", 1024 * 10, NULL, PRI_APP, NULL); // 系统初始化任务(一次性任务)

    while (1)
    {

        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(1000));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        // lv_timer_handler();
    }
}
