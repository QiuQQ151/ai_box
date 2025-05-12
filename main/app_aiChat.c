#include "lvgl.h"
#include "app.h"
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
#include "nvs_flash.h"
#include <string.h>
#include "esp_timer.h"
#include "driver/ledc.h"
#include "esp_netif.h"
#include "esp_lcd_panel_vendor.h"
#include "userbaidu_tts.h"
#include "userbaidu_vtt.h"
#include "userdeepseek.h"
#include "sysinfo.h"

// 外部变量声明
extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例

extern char ask_text[256];         // 存储提问的文字
extern char deepseek_content[2048]; // 存储AI回答的内容

// 本文件的全局变量声明
static const char *TAG = "AI_CHAT";
char *baidu_access_token = NULL;

int ask_flag = 0;
int answer_flag = 0;
int lcd_clear_flag = 0;
bool Is_in_deepseek = false; // 是否在AI聊天过程中（禁止deepseek中途打断退出，资源未释放）

// 主界面
LV_FONT_DECLARE(font_alipuhui20);
lv_obj_t *top_bar;
lv_obj_t *chat_disp; // 聊天的显示界面
lv_obj_t *label1;
lv_obj_t *label2;
lv_obj_t *wifi_icon;
lv_obj_t *time_label;
lv_obj_t *progress_bar;
lv_timer_t *update_aichatpage_timer;

// aichat_task
baidu_vtt_handle_t vtt;
baidu_tts_handle_t tts;
audio_event_iface_handle_t evt;
TaskHandle_t ai_chat_task_handle;

static void back_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED && Is_in_deepseek == false)
    {
        // 返回前准备
        ESP_LOGI(TAG, "AI Chat back button clicked");
        ESP_LOGI(TAG, "delete AI Chat task");
        ESP_LOGI(TAG, "release sound resource");
        es8311_pa_power(false); // 关闭音频
        baidu_vtt_destroy(vtt);
        baidu_tts_destroy(tts);
        /* Stop all periph before removing the listener */
        // esp_periph_set_stop_all(sys_resource.set);                                                // 停止所有外设
        audio_event_iface_remove_listener(esp_periph_set_get_event_iface(sys_resource.set), evt); // 移除事件监听器
        /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
        audio_event_iface_destroy(evt); // 销毁事件接口
        if (ai_chat_task_handle != NULL)
        {
            vTaskDelete(ai_chat_task_handle); // 删除AI聊天任务
            ai_chat_task_handle = NULL;
        }
        lv_timer_del(update_aichatpage_timer);
        lv_obj_del(top_bar); // 删除应用界面
        lv_obj_del(chat_disp); // 删除聊天的界面
        // 返回home
        app_to_home_prepare();
    }
}

static void value_update_cb(lv_timer_t *timer)
{
    // 状态栏更新
    lv_label_set_text(time_label, sys_info.current_time);

    lv_bar_set_value(progress_bar, sys_info.unused_mem, LV_ANIM_ON);

    // 用户提问更新
    if (ask_flag == 1)
    {
        lv_label_set_text_fmt(label1, "我：%s", ask_text);
        ask_flag = 0;

        // 滚动到最新内容
        lv_obj_scroll_to_y(lv_obj_get_parent(label1), 0, LV_ANIM_OFF);
    }

    // AI 回答更新
    if (answer_flag == 1)
    {
        lv_label_set_text_fmt(label2, "AI：%s", deepseek_content);
        answer_flag = 0;

        // 确保文本在可视区域内
        lv_obj_scroll_to_y(lv_obj_get_parent(label2), 0, LV_ANIM_OFF);

        // 重新计算高度（自动调整）
        lv_obj_set_height(label2, LV_SIZE_CONTENT);
        lv_obj_refr_size(label2);

        // 限制最大高度（避免超出可视区域）
        if (lv_obj_get_height(label2) > 160)
        {
            lv_obj_set_height(label2, 160);
        }
    }

    // 清屏操作
    if (lcd_clear_flag == 1)
    {
        lcd_clear_flag = 0;
        lv_label_set_text(label1, "我：");
        lv_label_set_text(label2, "AI：");

        // 重置滚动位置
        lv_obj_scroll_to_y(lv_obj_get_parent(label1), 0, LV_ANIM_OFF);
        lv_obj_scroll_to_y(lv_obj_get_parent(label2), 0, LV_ANIM_OFF);

        // 重置高度
        lv_obj_set_height(label2, LV_SIZE_CONTENT);
    }
}

void button_home_to_aiChat_handler(lv_event_t *e)
{
    // 进入前准备
    home_to_app_prepare();

    // ------------------------------图形显示-------------------------------
    // 设置黑色背景
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);

    /* 创建顶部状态栏 */
    top_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(top_bar, 320, 30);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE); // 关键：禁用滚动
    // 返回按钮（左上角）
    lv_obj_t *back_btn = lv_btn_create(top_bar);
    lv_obj_set_size(back_btn, 40, 30);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 0, -10);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE); // 也禁用按钮自身的滚动
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back_btn, 0, 0);       // 改为直角
    lv_obj_set_style_border_width(back_btn, 0, 0); // 移除边框
    // 按钮图标
    lv_obj_t *btn_label = lv_label_create(back_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_18, 0); // 稍大的图标
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(back_btn, back_btn_event_handler, LV_EVENT_CLICKED, NULL); // 点击事件回调
    // 添加按压效果
    lv_obj_set_style_transform_width(back_btn, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(back_btn, -2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x555555), LV_STATE_PRESSED);

    // 时间显示（中间）
    time_label = lv_label_create(top_bar);
    lv_label_set_text(time_label, "12:30");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_20, 0); // 稍大的图标
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x00BFFF), 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);

    /* 在状态栏右侧创建进度条 */
    progress_bar = lv_bar_create(top_bar);
    lv_obj_set_size(progress_bar, 100, 15);                // 设置进度条大小（宽度100，高度15）
    lv_obj_align(progress_bar, LV_ALIGN_RIGHT_MID, -5, 0); // 右侧居中，留5px边距
    lv_bar_set_range(progress_bar, 0, 400);                // 设置范围0-400
    lv_bar_set_value(progress_bar, 200, LV_ANIM_OFF);      // 设置初始值200（可以根据需要修改）

    /* 设置进度条样式 */
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x555555), LV_PART_MAIN); // 背景灰色
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(progress_bar, lv_color_hex(0xFFFFFF), LV_PART_MAIN); // 白色边框
    lv_obj_set_style_border_width(progress_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar, 3, LV_PART_MAIN); // 圆角3px

    /* 设置进度条指示器样式（填充部分） */
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x00BFFF), LV_PART_INDICATOR); // 蓝色指示器
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar, 3, LV_PART_INDICATOR); // 圆角3px

    // 主内容区域样式
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10); // 设置圆角半径
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
    lv_style_set_bg_color(&style, lv_color_hex(0x00BFFF));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 10);
    lv_style_set_width(&style, 320);
    lv_style_set_height(&style, 200); // 调整高度适应顶部状态栏

    // 主内容区域对象
    chat_disp = lv_obj_create(lv_scr_act());
    lv_obj_add_style(chat_disp, &style, 0);
    lv_obj_align(chat_disp, LV_ALIGN_BOTTOM_MID, 0, 0);

    // 用户输入显示
    label1 = lv_label_create(chat_disp);
    lv_obj_set_width(label1, 300);
    lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(label1, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(label1, &font_alipuhui20, LV_STATE_DEFAULT);
    lv_label_set_text(label1, "I:");

    // AI回复显示
    label2 = lv_label_create(chat_disp);
    lv_obj_set_width(label2, 300);
    lv_label_set_long_mode(label2, LV_LABEL_LONG_WRAP); // 改为自动换行
    lv_obj_align(label2, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_text_font(label2, &font_alipuhui20, LV_STATE_DEFAULT);
    lv_label_set_text(label2, "DeepSeek：");

    // 设置AI回复区域的最大高度，避免超出屏幕
    lv_obj_set_height(label2, 160); // 限制高度在可见范围内
    lv_obj_set_style_pad_all(label2, 5, 0);

    update_aichatpage_timer = lv_timer_create(value_update_cb, 100, NULL); // 创建一个lv_timer更新界面显示

    // --------------------------启动AI聊天任务--------------------------
    xTaskCreate(ai_chat_task, "ai_chat_task", 8192, NULL, 5, &ai_chat_task_handle); // ai聊天任务(前台)
}

/**************************** AI对话任务函数 **********************************/
void ai_chat_task(void *pv)
{
    ESP_LOGI(TAG, "start AI Chat task");
    // 百度token计算
    if (baidu_access_token == NULL)
    {
        // Must freed `baidu_access_token` after used
        baidu_access_token = baidu_get_access_token(sys_info.baidu_key, sys_info.baidu_secret);
    }
    // 百度 语音转文字 初始化
    baidu_vtt_config_t vtt_config = {
        .record_sample_rates = 16000,
        .encoding = ENCODING_LINEAR16,
    };
    vtt = baidu_vtt_init(&vtt_config);
    // 百度 文字转语音 初始化
    baidu_tts_config_t tts_config = {
        .playback_sample_rate = 16000,
    };
    tts = baidu_tts_init(&tts_config);
    // 创建监听“流”
    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);
    ESP_LOGI(TAG, "[4.1] Listening event from the pipeline"); // 从管道监听事件
    baidu_vtt_set_listener(vtt, evt);
    baidu_tts_set_listener(tts, evt);
    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(sys_resource.set), evt); // 从外设集合监听事件
    ESP_LOGI(TAG, "[ 5 ] Listen for all pipeline events");
    while (1) // 只要在AI聊天界面就一直循环 只到返回按钮按下
    {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) != ESP_OK)
        {
            ESP_LOGW(TAG, "[ * ] Event process failed: src_type:%d, source:%p cmd:%d, data:%p, data_len:%d",
                     msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);
            continue;
        }

        ESP_LOGI(TAG, "[ * ] Event received: src_type:%d, source:%p cmd:%d, data:%p, data_len:%d",
                 msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);

        if (baidu_tts_check_event_finish(tts, &msg))
        {
            ESP_LOGI(TAG, "[ * ] TTS Finish");
            es8311_pa_power(false); // 关闭音频
            continue;
        }

        if (msg.cmd == PERIPH_BUTTON_PRESSED)
        {
            baidu_tts_stop(tts);
            ESP_LOGI(TAG, "[ * ] Resuming pipeline");
            lcd_clear_flag = 1;
            baidu_vtt_start(vtt);
        }
        else if (msg.cmd == PERIPH_BUTTON_RELEASE || msg.cmd == PERIPH_BUTTON_LONG_RELEASE)
        {
            ESP_LOGI(TAG, "[ * ] Stop pipeline");

            char *original_text = baidu_vtt_stop(vtt);
            if (original_text == NULL)
            {
                deepseek_content[0] = 0; // 清空minimax 第1个字符写0就可以
                continue;
            }
            ESP_LOGI(TAG, "Original text = %s", original_text);
            ask_flag = 1;

            // char *answer = minimax_chat(original_text);
            Is_in_deepseek = true;
            char *answer = deepseek_chat(original_text); // 禁止打断
            Is_in_deepseek = false;
            if (answer == NULL)
            {
                continue;
            }
            ESP_LOGI(TAG, "minimax answer = %s", answer);
            answer_flag = 1;
            es8311_pa_power(true); // 打开音频
            baidu_tts_start(tts, answer);
        }
    }
    // vTaskDelete(NULL);
}
