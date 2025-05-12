#pragma once
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_peripherals.h"
#include "lvgl.h"

//
/* WiFi配置 - 请替换为您的实际配置 */
#define WIFI_SSID "FX53VD"
#define WIFI_PASS "1234567890"
/* SNTP服务器配置 */
#define PRIMARY_SNTP_SERVER "ntp.aliyun.com"
#define SECONDARY_SNTP_SERVER "cn.ntp.org.cn"
#define TIME_ZONE "CST-8" // 中国标准时区

// 任务优先级统一定义
#define PRI_IDLE 0  // 空闲
#define PRI_DBG 1   // 调试
#define PRI_INIT 1  // 初始化
#define PRI_SENS 2  // 传感器
#define PRI_APP 5   // 应用
#define PRI_DISP 10 // 显示
#define PRI_COMM 20 // 通信

// APP_ID定义
#define Page_APP_ID 0
#define Setting_APP_ID 1
#define Radio_APP_ID 2
#define Temp_humidity_APP_ID 3
#define BattleKedi_APP_ID 4
#define Test_APP_ID 5
#define Location_APP_ID 6
#define Wifi_APP_ID 7
#define Ble_APP_ID 8
#define Bettery_APP_ID 9
#define TOMATO_CLOCK_APP_ID 10

// 记录系统状态信息的结构体
typedef struct
{
    //-------------------系统配置信息-----------------------------
    char ssid[64];
    char password[128];
    char baidu_key[256];
    char baidu_secret[256];
    char deepseek_key[256];
    char deepseek_url[512];
    char deepseek_model[128];
    char deepseek_tipword[1024];

    //-------------------系统状态信息-----------------------------
    // nvs
    bool nvs_stata; // nvs是否正常（信息正常）

    // 电池
    int battery_level; // 电池电量（0-100）
    // wifi
    int wifi_signal_strength; // WiFi 信号强度（0-4）
    bool wifi_connected;      // WiFi 连接状态
    char wifi_ssid[32];       // WiFi SSID
    char wifi_ip[16];         // WiFi IP 地址
    char wifi_mac[18];        // WiFi MAC 地址

    // 蓝牙
    bool bluetooth_connected; // 蓝牙连接状态

    // 音量
    int volume_level; // 音量级别（0-100）
    bool is_muted;    // 静音状态

    // 时间
    char current_time[20]; // 当前时间字符串（格式：HH:MM:SS）

    // 系统状态
    bool is_playing; // 播放状态
    int unused_mem; // 剩余内存

} sys_info_t;
// 系统的资源接口
typedef struct
{
    esp_periph_set_handle_t set; // 外设集合句柄
    esp_periph_handle_t button_handle; // key


    // es8311
    SemaphoreHandle_t es8311_init_done; // 全局信号量句柄

    // LCD
    lv_disp_t *disp; // lvgl的屏幕句柄

    // key按压次数信息队列
    QueueHandle_t key_press_count_queue; // user_key.c

    // 发送给app的key次数指令信息队列
    QueueHandle_t key_to_app_queue; // app_manager.c

    int current_app_id; // 当前活动应用的ID

} sys_resource_t;
