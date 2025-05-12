#include "lvgl.h"
#include "app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "audio_idf_version.h"
#include "esp_wifi.h"
#include "audio_pipeline.h"
#include "audio_element.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "aac_decoder.h"
#include "mp3_decoder.h"
#include "board.h"
#include "sysinfo.h"

extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例
LV_FONT_DECLARE(font_alipuhui20);

static const char *TAG = "radio";

static TaskHandle_t radio_task_handle = NULL; // 音频任务句柄
static lv_timer_t *update_radio_timer;
// UI相关变量
static lv_obj_t *app_cont = NULL;   // 当前活动app应用容器
static lv_obj_t *return_btn = NULL; // 返回按钮

// Radio相关变量
typedef struct
{
    const char *name;
    const char *url;
} radio_station_t;

// 预设电台
static radio_station_t stations[] = {
    {"广东音乐之声", "http://lhttp.qingting.fm/live/4915/64k.mp3"},
   // {"广东新闻广播", "http://lhttp.qingting.fm/live/7846/64k.mp3"}, no
   // {"两广音乐之声", "http://lhttp.qingting.fm/live/18161/64k.mp3"}, no
    {"清晨音乐电台", "http://lhttp.qingting.fm/live/4915/64k.mp3"},
  //  {"动听音乐电台", "http://lhttp.qingting.fm/live/5930/64k.mp3"}, no
    {"星河音乐", "http://lhttp.qingting.fm/live/20210755/64k.mp3"},
    {"80后音乐台", "http://lhttp.qingting.fm/live/20207761/64k.mp3"},
   // {"90后潮流音悦台", "http://lhttp.qingting.fm/live/20207760/64k.mp3"}, no
    {"天籁古典", "http://lhttp.qingting.fm/live/20210756/64k.mp3"},
    {"民谣音乐台", "http://lhttp.qingting.fm/live/20207763/64k.mp3"},
    {"古典音乐厅", "http://lhttp.qingting.fm/live/20500181/64k.mp3"},
    {"Tiktok网络电台", "http://lhttp.qingting.fm/live/5062/64k.mp3"},
    {"摇滚天空台", "http://lhttp.qingting.fm/live/20207765/64k.mp3"},
    //{"鱼老音乐坊", "http://lhttp.qingting.fm/live/20500158/64k.mp3"},
    {"AsiaFM HD音乐台", "http://lhttp.qingting.fm/live/15318341/64k.mp3"},
    // {"FM 94.3", "http://lhttp.qingting.fm/live/4916/64k.mp3"}, 
};

static uint8_t current_station = 0;
static bool is_playing = false;
static bool is_muted = false;
static int vomle = 10; // 音量大小
// 音频管道
static audio_pipeline_handle_t pipeline = NULL;
static audio_element_handle_t http_stream = NULL;
static audio_element_handle_t mp3_decoder = NULL;
static audio_element_handle_t i2s_writer = NULL;
static lv_obj_t *play_label = NULL;

// UI控件
static lv_obj_t *time_label;
static lv_obj_t *wifi_icon;
static lv_obj_t *mute_icon;
static lv_obj_t *station_label;
static lv_obj_t *preset_btn;
static lv_obj_t *volume_slider;
static lv_obj_t *prev_btn;
static lv_obj_t *play_btn;
static lv_obj_t *next_btn;

// 从radio返回home
void button_radio_to_home_handler(lv_event_t *e)
{
    // 释放音频资源
    es8311_codec_set_voice_volume(0); // 设置初始音量
    es8311_pa_power(false);           //
    if (pipeline)
    {
        audio_pipeline_stop(pipeline);
        audio_pipeline_terminate(pipeline);
        audio_pipeline_unregister(pipeline, http_stream);
        audio_pipeline_unregister(pipeline, mp3_decoder);
        audio_pipeline_unregister(pipeline, i2s_writer);
        audio_pipeline_remove_listener(pipeline);
        audio_pipeline_deinit(pipeline);
    }
    if (http_stream)
        audio_element_deinit(http_stream);
    if (mp3_decoder)
        audio_element_deinit(mp3_decoder);
    if (i2s_writer)
        audio_element_deinit(i2s_writer);

    pipeline = NULL;
    http_stream = NULL;
    mp3_decoder = NULL;
    i2s_writer = NULL;

    is_muted = false;
    is_playing = false;
    current_station = 0;

    // 清理radio_task
    if (radio_task_handle)
    {
        vTaskDelete(radio_task_handle);
        radio_task_handle = NULL;
    }

    // 清理radio界面
    lv_timer_del(update_radio_timer);
    update_radio_timer = NULL;
    lv_obj_del(app_cont);
    app_cont = NULL;
    // 返回home界面
    app_to_home_prepare();
}

// -------------------------------ui 更新------------------------------------------------
void update_time()
{
    // 这里应该获取系统时间并更新time_label
    lv_label_set_text(time_label, sys_info.current_time);
}

void update_wifi_icon()
{
    // 这里应该根据wifi信号强度更新图标
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
}

void update_mute_icon() // 更新音量条
{
    lv_label_set_text(mute_icon, is_muted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
}

void update_station_info() // 更新播放电台名字
{
    lv_label_set_text(station_label, stations[current_station].name);
}

void update_radio_timer_cb(void)
{
    update_time();
}

// 播放控制
void radio_stop(void)
{
    if (pipeline)
    {
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
    }
}

void radio_play(void)
{
    if (pipeline)
    {
        audio_pipeline_reset_items_state(pipeline);
        audio_pipeline_reset_ringbuffer(pipeline);
        audio_element_set_uri(http_stream, stations[current_station].url);
        audio_pipeline_run(pipeline);
    }
}

void toggle_play_pause(lv_event_t *e)
{
    if (is_playing) // 从播放到停止
    {
        is_playing = false;
        ESP_LOGI(TAG, "stop");
        es8311_pa_power(false);
        radio_stop();
        lv_label_set_text(play_label, LV_SYMBOL_PLAY);
    }
    else
    {
        is_playing = true;
        ESP_LOGI(TAG, "play");
        radio_play();
        es8311_pa_power(true);
        lv_label_set_text(play_label, LV_SYMBOL_STOP);
    }
}

void toggle_mute()
{
    if (is_muted) // 处于静音
    {
        ESP_LOGI(TAG, "关闭静音");
        es8311_pa_power(true);
        is_muted = false;
    }
    else
    {
        ESP_LOGI(TAG, "静音");
        es8311_pa_power(false);
        is_muted = true;
    }
}

void prev_station() // 上一个电台
{
    current_station = (current_station == 0) ? sizeof(stations) / sizeof(stations[0]) - 1 : current_station - 1;
    update_station_info();
    if (is_playing == true)
    {
        radio_stop();
        radio_play();
    }
}

void next_station() // 下个电台
{
    current_station = (current_station + 1) % (sizeof(stations) / sizeof(stations[0]));
    update_station_info();
    if (is_playing == true)
    {
        radio_stop();
        radio_play();
    }
}

void show_preset_menu(lv_event_t *e)
{
    // 这里应该实现显示电台预设菜单的逻辑
    ESP_LOGI(TAG, "Showing preset menu");
}

void volume_changed(lv_event_t *e)
{
    int32_t volume = lv_slider_get_value(volume_slider);
    ESP_LOGI(TAG, "Volume changed to %" PRId32, volume);
    es8311_codec_set_voice_volume(volume);
}

void app_radio_task(void *arg) // 音频任务
{
    ESP_LOGI(TAG, "create pipeline...");

    // 创建音频管道
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline)
    {
        ESP_LOGE(TAG, "Failed to create pipeline");
        vTaskDelete(NULL);
        return;
    }

    // 配置 HTTP 流
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.type = AUDIO_STREAM_READER;
    http_cfg.enable_playlist_parser = false;
    http_cfg.out_rb_size = 8 * 1024;
    http_cfg.task_stack = 4 * 1024;
    http_cfg.request_size = 4096;
    http_stream = http_stream_init(&http_cfg);

    // 设置当前电台URL
    audio_element_set_uri(http_stream, stations[0].url); // 默认播放第1号电台

    // 配置 mp3解码器
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    // 配置 I2S 输出
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_writer = i2s_stream_init(&i2s_cfg);

    // 将组件注册到管道
    audio_pipeline_register(pipeline, http_stream, "http");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_writer, "i2s");

    // 链接管道组件
    const char *link_tag[3] = {"http", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    // // 启动管道
    ESP_LOGI(TAG, "Starting pipeline..."); // 先不启动
    // audio_pipeline_run(pipeline);

    es8311_codec_set_voice_volume(vomle); // 设置初始音量

    // 监听事件
    audio_event_iface_handle_t evt;
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);

    while (1) // 无限循环，直到外部删除任务
    {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) == ESP_OK)
        {
            ESP_LOGI(TAG, "Event received: src_type=%d, source=%p, cmd=%d, data=%p, data_len=%d",
                     msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);
        }
    }
}

void create_radio_ui()
{
    // 创建应用界面容器
    app_cont = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(app_cont);
    lv_obj_set_size(app_cont, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_bg_color(app_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(app_cont, LV_OPA_COVER, 0);

    // 返回按钮
    return_btn = lv_btn_create(app_cont);
    lv_obj_set_size(return_btn, 40, 30);
    lv_obj_align(return_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(return_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(return_btn, 5, 0);
    lv_obj_t *btn_label = lv_label_create(return_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_LEFT);
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(return_btn, button_radio_to_home_handler, LV_EVENT_CLICKED, NULL);

    // 时间显示
    time_label = lv_label_create(app_cont);
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_text_font(time_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(time_label, "00:00");

    // 右侧图标
    wifi_icon = lv_label_create(app_cont);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_align(wifi_icon, LV_ALIGN_TOP_RIGHT, -50, 15);

    mute_icon = lv_label_create(app_cont);
    lv_label_set_text(mute_icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_align(mute_icon, LV_ALIGN_TOP_RIGHT, -10, 15);
    lv_obj_add_event_cb(mute_icon, (lv_event_cb_t)toggle_mute, LV_EVENT_CLICKED, NULL);

    // 中间部分
    // 电台名称
    station_label = lv_label_create(app_cont);
    lv_obj_set_style_text_font(station_label, &font_alipuhui20, 0);
    lv_obj_set_style_text_color(station_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(station_label, LV_ALIGN_CENTER, 0, -30);
    update_station_info();

    // 预设电台按钮
    preset_btn = lv_btn_create(app_cont);
    lv_obj_set_size(preset_btn, 40, 30);
    lv_obj_align(preset_btn, LV_ALIGN_CENTER, 120, -30);
    lv_obj_t *preset_label = lv_label_create(preset_btn);
    lv_label_set_text(preset_label, LV_SYMBOL_LIST);
    lv_obj_center(preset_label);
    lv_obj_add_event_cb(preset_btn, show_preset_menu, LV_EVENT_CLICKED, NULL);

    // 音量条
    volume_slider = lv_slider_create(app_cont);
    lv_obj_set_width(volume_slider, 250);
    lv_obj_align(volume_slider, LV_ALIGN_CENTER, 0, 20);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, vomle, LV_ANIM_OFF);
    lv_obj_add_event_cb(volume_slider, volume_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // 底部控制栏
    // 上一个电台按钮
    prev_btn = lv_btn_create(app_cont);
    lv_obj_set_size(prev_btn, 50, 50);
    lv_obj_align(prev_btn, LV_ALIGN_BOTTOM_LEFT, 30, -20);
    lv_obj_t *prev_label = lv_label_create(prev_btn);
    lv_label_set_text(prev_label, LV_SYMBOL_PREV);
    lv_obj_center(prev_label);
    lv_obj_add_event_cb(prev_btn, (lv_event_cb_t)prev_station, LV_EVENT_CLICKED, NULL);

    // 播放/暂停按钮
    play_btn = lv_btn_create(app_cont);
    lv_obj_set_size(play_btn, 60, 60);
    lv_obj_align(play_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    play_label = lv_label_create(play_btn);
    lv_label_set_text(play_label, LV_SYMBOL_PLAY);
    lv_obj_center(play_label);
    lv_obj_add_event_cb(play_btn, toggle_play_pause, LV_EVENT_CLICKED, NULL);

    // 下一个电台按钮
    next_btn = lv_btn_create(app_cont);
    lv_obj_set_size(next_btn, 50, 50);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_RIGHT, -30, -20);
    lv_obj_t *next_label = lv_label_create(next_btn);
    lv_label_set_text(next_label, LV_SYMBOL_NEXT);
    lv_obj_center(next_label);
    lv_obj_add_event_cb(next_btn, (lv_event_cb_t)next_station, LV_EVENT_CLICKED, NULL);

    update_radio_timer = lv_timer_create(update_radio_timer_cb, 100, NULL); // 创建一个lv_timer更新界面显示
}

void button_home_to_radio_handler(lv_event_t *e)
{
    home_to_app_prepare(); // 隐藏home主界面
    // 初始化参数
    is_muted = false;
    is_playing = false;
    current_station = 0;

    // 创建收音机界面
    create_radio_ui();

    // 创建任务更新时间和状态
    if (radio_task_handle == NULL)
    {
        xTaskCreate(app_radio_task, "app_radio_task", 1024 * 4, NULL, 10, &radio_task_handle);
    }
}

// #include "lvgl.h"
// #include "app.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_log.h"
// #include "audio_idf_version.h"
// #include "esp_wifi.h"
// #include "esp_http_client.h"
// #include "baidu_access_token.h"
// #include "esp_peripherals.h"
// #include "periph_button.h"
// #include "periph_wifi.h"
// #include "nvs_flash.h"
// #include <string.h>
// #include "esp_timer.h"
// #include "driver/ledc.h"
// #include "esp_netif.h"
// #include "esp_lcd_panel_vendor.h"

// #include "audio_pipeline.h"
// #include "audio_element.h"
// #include "audio_event_iface.h"
// #include "audio_common.h"
// #include "http_stream.h"
// #include "i2s_stream.h"
// #include "aac_decoder.h"
// #include "mp3_decoder.h"
// #include "board.h"
// #include "sysinfo.h"

// // 外部变量声明
// extern sys_info_t sys_info;         // 系统状态信息结构体实例
// extern sys_resource_t sys_resource; // 系统资源接口结构体实例

// TaskHandle_t radio_task_handle;
// static const char *TAG = "RADIO";

// void radio_task(void *arg)
// {
//     ESP_LOGI(TAG, "create pipeline...");
//     // 2. 创建音频管道
//     audio_pipeline_handle_t pipeline;
//     audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
//     pipeline = audio_pipeline_init(&pipeline_cfg);

//     // 3. 配置 HTTP 流（RTSP 协议）
//     http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
//     http_cfg.type = AUDIO_STREAM_READER;
//     http_cfg.enable_playlist_parser = false;
//     http_cfg.out_rb_size = 8 * 1024; // 8KB环形缓冲区
//     http_cfg.task_stack = 4 * 1024;  // 4KB任务堆栈
//     http_cfg.request_size = 4096;    // 每次请求4KB数据
//     audio_element_handle_t http_stream = http_stream_init(&http_cfg);

//     // 设置 电台 URL
//     // audio_element_set_uri(http_stream, "http://stream.radioparadise.com/mp3-192"); // 可用
//     // audio_element_set_uri(http_stream, "http://music.163.com/song/media/outer/url?id=1363948882.mp3");  // 可用
//     // audio_element_set_uri(http_stream, "http://lhttp.qingting.fm/live/4915/64k.mp3"); //
//     audio_element_set_uri(http_stream, "http://lhttp.qingting.fm/live/20207761/64k.mp3");
//     // 4. 配置 mp3解码器
//     mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
//     audio_element_handle_t mp3_decoder = mp3_decoder_init(&mp3_cfg);

//     // 5. 配置 I2S 输出（或 PWM 输出）
//     i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
//     i2s_cfg.type = AUDIO_STREAM_WRITER;
//     audio_element_handle_t i2s_writer = i2s_stream_init(&i2s_cfg);

//     // 6. 将组件注册到管道
//     audio_pipeline_register(pipeline, http_stream, "http");
//     audio_pipeline_register(pipeline, mp3_decoder, "mp3");
//     audio_pipeline_register(pipeline, i2s_writer, "i2s");

//     // 7. 链接管道组件
//     const char *link_tag[3] = {"http", "mp3", "i2s"};
//     audio_pipeline_link(pipeline, &link_tag[0], 3);

//     // 8. 启动管道
//     ESP_LOGI(TAG, "Starting pipeline...");
//     audio_pipeline_run(pipeline);
//     // 9. 监听事件（可选）
//     audio_event_iface_handle_t evt;
//     audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
//     evt = audio_event_iface_init(&evt_cfg);
//     audio_pipeline_set_listener(pipeline, evt);

//     es8311_codec_set_voice_volume(50); //
//     es8311_pa_power(true);             //

//     while (1)
//     {
//         audio_event_iface_msg_t msg;
//         if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) == ESP_OK)
//         {
//             ESP_LOGI(TAG, "Event received: src_type=%d, source=%p, cmd=%d, data=%p, data_len=%d",
//                      msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);
//         }
//     }

//     // 10. 清理资源（通常不会执行到这里）
//     audio_pipeline_stop(pipeline);
//     audio_pipeline_deinit(pipeline);
//     audio_event_iface_destroy(evt);
// }

// void button_home_to_radio_handler(lv_event_t *e)
// {
//     home_to_app_prepare();
//     ESP_LOGI(TAG, "Starting radio...");
//     xTaskCreate(radio_task, "radio_task", 1024 * 4, NULL, PRI_APP, &radio_task_handle);
// }