#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_peripherals.h"
#include "periph_button.h"

#include "board.h"
#include "esp_log.h"
#include "mykey.h"
#include "sysinfo.h"
static const char *TAG = "key";
extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例


void key_init_task(void *prava)
{
    ESP_LOGI(TAG, "Start key init");
    // Initialize Button peripheral
    periph_button_cfg_t btn_cfg = {
        .gpio_mask = (1ULL << get_input_mode_id()) | (1ULL << get_input_rec_id()),
    };
    esp_periph_handle_t button_handle = periph_button_init(&btn_cfg);
    sys_resource.button_handle = button_handle;
    // Start button peripheral
    esp_periph_start(sys_resource.set, button_handle);

    // while(1){
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }
  //vTaskDelete(NULL);
}