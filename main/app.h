#pragma once
#include "lvgl.h"

void app_aiChat(void);
void ai_chat_task(void *pv);



// home页面的管理函数---------------------------------------------------------
void app_home(void);  // home启动页面
void app_to_home_prepare(void); // app返回home的入口函数
void home_to_app_prepare(void); // 隐藏home主界面
void set_selected_icon(int index); // 设定索引更改选中的app
void home_active_to_key_task(void* arg); // 在home界面处理key事件
void home_bar_task(void* arg);  // 状态栏更新
// //  状态栏相关接口函数
void update_status_bar_time(const char *time_str);
void set_alarm_icon_state(bool enabled);
void set_bluetooth_icon_state(bool enabled, bool connected);
void set_wifi_icon_state(bool connected, int signal_strength);
void set_mute_icon_state(bool muted);
void set_battery_icon_state(int level, bool charging);


// 从home进入app的入口函数----------------------------------------------------
// 样例：void music_button_handler(lv_event_t *e) 
// {
//     hide_home_base_cont(); // 隐藏home主界面 
//     // app界面部分
//     执行的内容。。。。
//     返回时调用void button_app_to_home_handler(lv_event_t * e); // app返回home的入口函数  
//     删除当前容器界面，恢复home主界面
// }
void button_home_to_aiChat_handler(lv_event_t *e);  // aiChat 应用入口
void button_home_to_tomatoColock_handle(lv_event_t *e); // 番茄时钟
void button_home_to_test_handler(lv_event_t *e);
void button_home_to_setting_handler(lv_event_t *e);
void button_home_to_radio_handler(lv_event_t *e);
void button_home_to_temp_humidity_handler(lv_event_t *e);
void button_home_to_battleKedi_handler(lv_event_t *e);
