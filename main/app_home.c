#include "stdio.h"
#include "stdlib.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_idf_version.h"
#include "esp_peripherals.h"
#include "periph_button.h"
#include "periph_wifi.h"

#include "sysinfo.h"
#include "app.h"

//
extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例

//
static const char *TAG = "home station";
audio_event_iface_handle_t home_evt;
TaskHandle_t home_key_task_handle;
#define ICON_SIZE 100
#define ICON_SPACING 20
#define STATUS_BAR_HEIGHT 24 // 状态栏高度

// 全局变量声明 图形界面元素
static int current_selected_index = 0;  // 当前选中索引
static lv_obj_t *scroll_cont = NULL;    // 滚动容器指针
static lv_obj_t *status_bar = NULL;     // 状态栏指针
static lv_obj_t *time_label = NULL;     // 时间标签
static lv_obj_t *alarm_icon = NULL;     // 闹钟图标
static lv_obj_t *bluetooth_icon = NULL; // 蓝牙图标
static lv_obj_t *wifi_icon = NULL;      // WiFi图标
static lv_obj_t *mute_icon = NULL;      // 静音图标
static lv_obj_t *battery_icon = NULL;   // 电池图标
static lv_obj_t *home_base_cont = NULL; // 基础容器指针(home界面)
static bool Is_in_home = true;          // 是否位于桌面

// 应用程序名称、图标和按下按钮回调的映射表
typedef struct
{
    const lv_img_dsc_t *icon;
    const char *name;
    lv_event_cb_t handler; // 回调函数指针
    bool Is_front_app;     // 是否是前台app
    int app_id;
} app_info_t;

// 图标
LV_IMG_DECLARE(icon_deepseek);
LV_IMG_DECLARE(icon_clock);
LV_IMG_DECLARE(icon_battle);
LV_IMG_DECLARE(icon_radio);
LV_IMG_DECLARE(rabi);
LV_IMG_DECLARE(img_bilibili120);
LV_FONT_DECLARE(font_alipuhui20);
// 注册的app图标的信息
static app_info_t apps[] = {
    {&icon_deepseek, "AI 对话", button_home_to_aiChat_handler, false, Test_APP_ID},
    {&icon_clock, "番茄时钟", button_home_to_tomatoColock_handle, false, Setting_APP_ID},
    {&icon_battle, "打地鼠", button_home_to_test_handler, false, Radio_APP_ID},
    {&icon_radio, "网络收音机", button_home_to_radio_handler, false, Temp_humidity_APP_ID},
};

// --------------------------------------------定义的app的调用函数-----------------------------------
// app界面返回home桌面的按钮事件处理
void app_to_home_prepare(void)
{
    Is_in_home = true;                                     // Home设置为前台
    lv_obj_clear_flag(home_base_cont, LV_OBJ_FLAG_HIDDEN); // 显示home主界面
}

// home到app的准备工作
void home_to_app_prepare(void)
{
    sys_resource.current_app_id = apps[current_selected_index].app_id;
    Is_in_home = false; // Home设置为后台
    lv_obj_add_flag(home_base_cont, LV_OBJ_FLAG_HIDDEN);
}

// home 界面内部使用函数
// 设置当前选中图标  可供按键事件使用，通过改变index更新被选中的图标
void set_selected_icon(int index)
{
    if (scroll_cont == NULL)
        return; // 安全检查

    int icon_num = lv_obj_get_child_cnt(scroll_cont); // 获取滚动容器中的子项数量
    if (index < 0 || index >= icon_num)
        return; // 检查索引是否有效（0 <= index < 子项数量）

    // 恢复旧选中项样式
    lv_obj_t *old_child = lv_obj_get_child(scroll_cont, current_selected_index); // 获取当前选中的子对象（根据current_selected_index）
    if (old_child)
    {
        lv_obj_t *old_label = lv_obj_get_child(old_child, 0);             // 获取子对象中的文本标签（假设是第一个子元素）
        lv_obj_set_style_text_font(old_label, &lv_font_montserrat_20, 0); // 恢复字体大小和颜色
        lv_obj_set_style_text_color(old_label, lv_color_hex(0xE0E0E0), 0);
    }

    // 设置新选中项样式
    lv_obj_t *new_child = lv_obj_get_child(scroll_cont, index);
    if (new_child)
    {
        lv_obj_t *new_label = lv_obj_get_child(new_child, 0);             // 获取新子对象中的文本标签
        lv_obj_set_style_text_font(new_label, &lv_font_montserrat_48, 0); // 设置高亮样式：大字体+绿色
        lv_obj_set_style_text_color(new_label, lv_color_hex(0x00FF00), 0);

        current_selected_index = index; // 更新当前选中索引
        lv_obj_scroll_to_view(new_child, LV_ANIM_ON);
    }
}

// -----------------------------------------------------home界面的状态栏定义接口-------------------------------------------
// 状态栏相关接口函数
void set_bar_time(const char *time_str)
{
    if (time_label)
    {
        lv_label_set_text(time_label, time_str);
    }
}

void set_alarm_icon_state(bool enabled)
{
    if (alarm_icon)
    {
        lv_obj_set_style_text_color(alarm_icon,
                                    enabled ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x404040), 0);
    }
}

void set_bluetooth_icon_state(bool enabled, bool connected)
{
    if (bluetooth_icon)
    {
        lv_obj_set_style_text_color(bluetooth_icon,
                                    connected ? lv_color_hex(0x0080FF) : (enabled ? lv_color_hex(0x808080) : lv_color_hex(0x404040)), 0);
    }
}

void set_wifi_icon_state(bool connected, int signal_strength)
{
    if (wifi_icon == NULL)
    {
        return;
    }

// 使用宏定义颜色值（编译时常量）
#define COLOR_DISCONNECTED lv_color_hex(0xFF0000) // 红色
#define COLOR_BASE lv_color_hex(0x808080)         // 灰色
#define COLOR_SIGNAL lv_color_hex(0x00FF00)       // 绿色

    // 确保信号强度在0-4范围内
    signal_strength = signal_strength < 0 ? 0 : (signal_strength > 4 ? 4 : signal_strength);

    if (!connected)
    {
        // 未连接状态 - 红色
        lv_obj_set_style_text_color(wifi_icon, COLOR_DISCONNECTED, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else
    {
        // 根据信号强度计算颜色
        uint8_t mix_ratio = signal_strength * 25; // 0=0%, 1=25%, 2=50%, 3=75%, 4=100%
        lv_color_t mixed_color = lv_color_mix(COLOR_SIGNAL, COLOR_BASE, mix_ratio);

        lv_obj_set_style_text_color(wifi_icon, mixed_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void set_mute_icon_state(bool muted)
{
    if (mute_icon)
    {
        lv_obj_set_style_text_color(mute_icon,
                                    muted ? lv_color_hex(0xFF0000) : lv_color_hex(0x808080), 0);
    }
}

void set_battery_icon_state(int level, bool charging)
{
    if (battery_icon)
    {
        const char *icon;
        lv_color_t color;

        if (charging)
        {
            icon = LV_SYMBOL_CHARGE;
            color = lv_color_hex(0x00FF00);
        }
        else if (level > 70)
        {
            icon = LV_SYMBOL_BATTERY_FULL;
            color = lv_color_hex(0x00FF00);
        }
        else if (level > 30)
        {
            icon = LV_SYMBOL_BATTERY_3;
            color = lv_color_hex(0xFFFF00);
        }
        else
        {
            icon = LV_SYMBOL_BATTERY_1;
            color = lv_color_hex(0xFF0000);
        }

        lv_label_set_text(battery_icon, icon);
        lv_obj_set_style_text_color(battery_icon, color, 0);
    }
}

// 创建状态栏
static void create_status_bar(lv_obj_t *parent)
{
    status_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(status_bar);
    lv_obj_set_size(status_bar, lv_disp_get_hor_res(NULL), STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(status_bar, 8, 0);
    lv_obj_set_style_pad_right(status_bar, 8, 0);
    lv_obj_set_style_pad_gap(status_bar, 16, 0);

    time_label = lv_label_create(status_bar);
    lv_label_set_text(time_label, "00:00");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *spacer = lv_obj_create(status_bar);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);

    alarm_icon = lv_label_create(status_bar);
    lv_label_set_text(alarm_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(alarm_icon, &lv_font_montserrat_16, 0);
    set_alarm_icon_state(false);

    bluetooth_icon = lv_label_create(status_bar);
    lv_label_set_text(bluetooth_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(bluetooth_icon, &lv_font_montserrat_16, 0);
    set_bluetooth_icon_state(false, false);

    wifi_icon = lv_label_create(status_bar);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_16, 0);
    set_wifi_icon_state(false, 0);

    mute_icon = lv_label_create(status_bar);
    lv_label_set_text(mute_icon, LV_SYMBOL_VOLUME_MID);
    lv_obj_set_style_text_font(mute_icon, &lv_font_montserrat_16, 0);
    set_mute_icon_state(false);

    battery_icon = lv_label_create(status_bar);
    lv_label_set_text(battery_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(battery_icon, &lv_font_montserrat_16, 0);
    set_battery_icon_state(100, false);
}

// 滚动结束事件的回调函数，自动将最靠近屏幕中心的子项设为选中状态
static void scroll_event_cb(lv_event_t *e)
{
    lv_obj_t *scroll_cont = lv_event_get_target(e);
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_SCROLL_END)
    {
        lv_coord_t scroll_x = lv_obj_get_scroll_x(scroll_cont);
        lv_coord_t cont_width = lv_obj_get_width(scroll_cont);
        lv_coord_t screen_mid = scroll_x + cont_width / 2;

        uint32_t child_cnt = lv_obj_get_child_cnt(scroll_cont);
        int32_t min_diff = INT32_MAX;
        int new_index = current_selected_index;

        for (uint32_t i = 0; i < child_cnt; i++)
        {
            lv_obj_t *child = lv_obj_get_child(scroll_cont, i);
            lv_coord_t child_center = lv_obj_get_x(child) + ICON_SIZE / 2;
            int32_t diff = LV_ABS(child_center - screen_mid);

            if (diff < min_diff)
            {
                min_diff = diff;
                new_index = i;
            }
        }

        if (new_index != current_selected_index)
        {
            set_selected_icon(new_index);
        }

        lv_coord_t max_scroll = lv_obj_get_scroll_left(scroll_cont);
        if (lv_obj_get_scroll_x(scroll_cont) > max_scroll)
        {
            lv_obj_scroll_to_x(scroll_cont, max_scroll, LV_ANIM_OFF);
        }
    }
}

void app_home(void)
{
    lv_disp_t *disp = sys_resource.disp;             // 获取主显示器
    lv_coord_t screen_w = lv_disp_get_hor_res(disp); // 显示器的尺寸
    lv_coord_t screen_h = lv_disp_get_ver_res(disp);

    // 创建主界面容器 // 创建全屏基础容器作为根对象
    home_base_cont = lv_obj_create(lv_scr_act());                         // 在活动屏幕上创建基础容器
    lv_obj_remove_style_all(home_base_cont);                              // 移除所有默认样式
    lv_obj_set_size(home_base_cont, screen_w, screen_h);                  // 设置容器大小为全屏
    lv_obj_set_style_bg_color(home_base_cont, lv_color_hex(0x000000), 0); // 背景黑色
    lv_obj_set_style_bg_opa(home_base_cont, LV_OPA_COVER, 0);             // 完全不透明度

    // 创建主要内容容器(减去状态栏高度)
    lv_obj_t *content_cont = lv_obj_create(home_base_cont);                                                // 在主界面容器基础上创建主要内容容器
    lv_obj_remove_style_all(content_cont);                                                                 // 清除默认样式
    lv_obj_set_size(content_cont, screen_w, screen_h - STATUS_BAR_HEIGHT);                                 // 设置容器尺寸(宽度全屏，高度减去状态栏)
    lv_obj_align(content_cont, LV_ALIGN_BOTTOM_MID, 0, 0);                                                 // 对齐到底部中间
    lv_obj_set_flex_flow(content_cont, LV_FLEX_FLOW_COLUMN);                                               // 设置为纵向弹性布局
    lv_obj_set_flex_align(content_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); // 设置弹性对齐方式(所有方向居中)

    // 创建状态栏  // 创建状态栏并放置在顶部
    create_status_bar(home_base_cont);                // 在基础容器基础上
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0); // 对齐到顶部中间

    // 创建滚动容器  // 创建水平滚动容器(用于图标轮播)
    scroll_cont = lv_obj_create(content_cont);                             // 在主要内容容器基础上
    lv_obj_remove_style_all(scroll_cont);                                  // 清除默认样式
    lv_obj_set_size(scroll_cont, screen_w, ICON_SIZE + 100);                // 设置滚动容器尺寸(高度=图标大小+50像素边距)
    lv_obj_align(scroll_cont, LV_ALIGN_TOP_MID, 0, 140);                 // 从顶部中间开始，向下偏移120像素
    lv_obj_set_style_max_width(scroll_cont, screen_w, 0);                  // 最大宽度限制
    lv_obj_set_flex_flow(scroll_cont, LV_FLEX_FLOW_ROW);                   // 设置为横向弹性布局
    lv_obj_set_style_pad_gap(scroll_cont, ICON_SPACING, 0);                // 设置图标间距
    lv_obj_set_scroll_snap_x(scroll_cont, LV_SCROLL_SNAP_CENTER);          // 配置滚动行为：中心对齐吸附效果
    lv_obj_set_scroll_dir(scroll_cont, LV_DIR_HOR);                        // 只允许水平滚动
    lv_obj_add_event_cb(scroll_cont, scroll_event_cb, LV_EVENT_ALL, NULL); // 添加滚动事件回调(处理滚动结束时的吸附效果)

    /* 隐藏滚动条 */
    lv_obj_set_style_height(scroll_cont, 0, LV_PART_SCROLLBAR);             // 高度设为0
    lv_obj_set_style_bg_opa(scroll_cont, LV_OPA_TRANSP, LV_PART_SCROLLBAR); // 完全透明
    lv_obj_set_style_border_width(scroll_cont, 0, LV_PART_SCROLLBAR);       // 无边框

    // 创建应用图标
    // 循环创建所有应用图标 定义在static const app_info_t apps[]中
    for (int i = 0; i < (sizeof(apps) / sizeof(apps[0])); i++)
    {
        lv_obj_t *icon_btn = lv_btn_create(scroll_cont); // 创建按钮对象
        // 图标创建
        lv_obj_remove_style_all(icon_btn);                              // 清除默认样式
        lv_obj_set_size(icon_btn, ICON_SIZE+40, ICON_SIZE+40);                // 设置按钮大小
        lv_obj_set_style_border_width(icon_btn, 0, LV_PART_MAIN);       // 无边框
        lv_obj_set_style_bg_opa(icon_btn, LV_OPA_TRANSP, LV_PART_MAIN); // 完全透明
        // lv_obj_set_style_border_color(icon_btn, lv_color_hex(0xFF0000), 0);
        // lv_obj_set_style_border_width(icon_btn, 2, 0);
        lv_obj_set_style_radius(icon_btn, 0, LV_PART_MAIN);             // 无圆角
        // icon
        lv_obj_t *img = lv_img_create(icon_btn);
        lv_img_set_src(img, apps[i].icon);
        lv_obj_set_size(img, apps[i].icon->header.w, apps[i].icon->header.h);
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
        // 创建icon文本标签
        lv_obj_t *label = lv_label_create(icon_btn);
        lv_label_set_text(label, apps[i].name);
        // 设置icon文本样式
        lv_obj_set_style_text_font(label, &font_alipuhui20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
        // 对齐文本到图标下方
        lv_obj_align_to(label, img, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
        // 添加点击事件  // 此处为切换应用的入口
        lv_obj_add_event_cb(icon_btn, apps[i].handler, LV_EVENT_CLICKED, NULL); // 点击事件回调
    }
    // 创建一个任务用于处理key按键
    // xTaskCreate(home_active_to_key_task, "home_active_to_key_task", 1024 * 2, NULL, 10, &home_key_task_handle); // 只用了不到1kb 有bug，
    // 创建一个任务用于更新home的状态栏
    xTaskCreate(home_bar_task, "home_bar_task", 1024 * 1, NULL, PRI_SENS, NULL); // 只用了不到1kb
    // 初始化选中第一个图标
    set_selected_icon(0);
    Is_in_home = true; // 设置为home界面
}

void home_active_to_key_task(void *arg)
{
    int icon_num = current_selected_index; // 获取当前选中图标的索引
    int key_count = 0;
    // 创建监听“流”
    ESP_LOGI(TAG, "Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    home_evt = audio_event_iface_init(&evt_cfg);
    ESP_LOGI(TAG, "Listening event from peripherals");
    audio_event_iface_set_listener(sys_resource.button_handle, home_evt); // 从外设集合监听事件
    while (1)
    {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(home_evt, &msg, portMAX_DELAY) != ESP_OK)
        {
            ESP_LOGW(TAG, "[ * ] Event process failed: src_type:%d, source:%p cmd:%d, data:%p, data_len:%d",
                     msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);
            continue;
        }

        ESP_LOGI(TAG, "[ * ] Event received: src_type:%d, source:%p cmd:%d, data:%p, data_len:%d",
                 msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);

        // 按钮的状态记录
        if (msg.cmd == PERIPH_BUTTON_RELEASE) // 短按下并释放
        {
            ESP_LOGI(TAG, "点按");
            icon_num += 1;
            if (icon_num >= (sizeof(apps) / sizeof(apps[0])))
            {
                icon_num = 0;
            }
            set_selected_icon(icon_num);
        }
        else if (msg.cmd == PERIPH_BUTTON_LONG_PRESSED) // 长按下
        {
            ESP_LOGI(TAG, "长按进入");
        }
    }
}

void home_bar_task(void *arg)
{

    while (1)
    {
        if (Is_in_home == true)
        {
            // ESP_LOGI(TAG, "update home bar task");
            //  更新状态栏
            set_bar_time(sys_info.current_time);
            set_alarm_icon_state(sys_info.volume_level);
            set_bluetooth_icon_state(sys_info.battery_level, sys_info.bluetooth_connected);
            set_wifi_icon_state(sys_info.wifi_connected, sys_info.wifi_signal_strength);
            set_mute_icon_state(sys_info.is_muted);
            set_battery_icon_state(sys_info.battery_level, true);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}