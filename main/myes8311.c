#include <string.h>
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "audio_idf_version.h"
#include "esp_peripherals.h"
#include "esp_timer.h"
#include "sysinfo.h"

extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例

static const char *TAG = "esp8311";

void es8311_init_task(void *arg)
{

    /*******  初始化ES8311音频芯片 (注意：这个里面会初始化I2C 后面初始化触摸屏就不用再初始化I2C了)  ********/
    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    es8311_codec_set_voice_volume(60); // 
    es8311_pa_power(false);            // 关闭声音
    // 通知lvgl的任务
    TaskHandle_t task_handle = (TaskHandle_t)arg;
    xTaskNotify(task_handle, 1, eSetValueWithOverwrite); // 通知值1

    // while (1)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }
    ESP_LOGI(TAG, "delete es8311_init_task");
    vTaskDelete(NULL); // 删除当前任务
}