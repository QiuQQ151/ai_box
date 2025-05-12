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
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_heap_caps.h"
//
#include "mywifi.h"
//
#include "usersntp.h"
#include "usertask.h"
#include "sysinfo.h"

extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例

// 用于输出调试信息的任务
// 该任务会定期输出系统状态信息，包括可用堆内存和任务列表
void DBG_task(void *arg)
{

    while (1)
    {
        // 辅助调试信息输出
        printf("===== 系统状态监控 =====\n");

        // 打印剩余堆内存（KB）
        int unused_mem = ((int)esp_get_free_heap_size()) / 1024;
        ESP_LOGI("MEM", "可用堆内存: %d KB", unused_mem);
        sys_info.unused_mem = unused_mem;

        // 获取任务列表
        char task_list[512];  // 存储任务信息的缓冲区
        vTaskList(task_list); // 获取FreeRTOS任务列表

        // 添加中文表头说明
        printf("\n"
               "| 任务名         | 状态  | 优先级 | 剩余栈(KB) | 任务ID |\n"
               "|----------------|-------|--------|------------|--------|\n");

        // 解析原始任务数据并转换单位
        char *line = strtok(task_list, "\n");
        while (line != NULL)
        {
            char name[16];    // 任务名
            char state;       // 状态
            unsigned prio;    // 优先级
            unsigned stack;   // 剩余栈(单位：sizeof(StackType_t))
            unsigned taskNum; // 任务ID

            // 解析每行数据（假设格式为：Name State Priority StackFree TaskNum）
            if (sscanf(line, "%15s %c %u %u %u", name, &state, &prio, &stack, &taskNum) == 5)
            {
                // 转换栈剩余量为KB（假设StackType_t为4字节）
                float stack_kb = (stack * sizeof(StackType_t)) / 1024.0f;

                // 状态转中文说明
                const char *state_cn = "";
                switch (state)
                {
                case 'R':
                    state_cn = "运行";
                    break;
                case 'B':
                    state_cn = "阻塞";
                    break;
                case 'S':
                    state_cn = "挂起";
                    break;
                case 'D':
                    state_cn = "删除";
                    break;
                default:
                    state_cn = "未知";
                    break;
                }

                // 格式化输出（对齐表格）
                printf("| %-14s | %-4s  | %-6u | %-10.2f | %-6u |\n",
                       name, state_cn, prio, stack_kb, taskNum);
            }
            line = strtok(NULL, "\n");
        }

        // 间隔5秒后再次刷新
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}


