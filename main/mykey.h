#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// 初始化按键组件（在app_main中调用）
void key_init_task(void* prava);

// 获取按键事件队列句柄（供其他组件使用）
QueueHandle_t key_get_event_queue(void);
