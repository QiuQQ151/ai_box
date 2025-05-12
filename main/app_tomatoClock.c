#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sysinfo.h"
#include "app.h"

extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例

LV_FONT_DECLARE(font_alipuhui20);
static lv_obj_t *app_cont = NULL;             // 当前活动app应用容器
static lv_obj_t *return_btn = NULL;           // 返回按钮
static lv_obj_t *chage_btn = NULL;
static TaskHandle_t app_tomato_handle = NULL; // 避免重复创建任务
static lv_obj_t *timer_label = NULL;
static lv_obj_t *state_label = NULL;
static lv_obj_t *start_stop_btn = NULL;
static lv_obj_t *reset_btn = NULL;

// 番茄时钟状态
typedef enum
{
    TOMATO_STATE_IDLE,    // 空闲状态
    TOMATO_STATE_WORKING, // 工作中
    TOMATO_STATE_BREAK    // 休息中
} tomato_state_t;

// 番茄时钟配置
typedef struct
{
    uint32_t work_duration;  // 工作时间(秒)
    uint32_t break_duration; // 休息时间(秒)
    uint32_t remaining_time; // 剩余时间(秒)
    tomato_state_t state;    // 当前状态
    bool is_running;         // 是否正在运行
} tomato_clock_t;

static tomato_clock_t tomato_clock = {
    .work_duration = 20 * 60, // 默认25分钟工作时间
    .break_duration = 5 * 60, // 默认5分钟休息时间
    .remaining_time = 20 * 60,
    .state = TOMATO_STATE_IDLE,
    .is_running = false};

// 更新显示时间
static void update_timer_display()
{
    uint32_t minutes = tomato_clock.remaining_time / 60;
    uint32_t seconds = tomato_clock.remaining_time % 60;
    // 使用足够大的缓冲区并修正格式化字符串
    char time_str[16]; // 增加缓冲区大小
    snprintf(time_str, sizeof(time_str), "%02" PRIu32 ":%02" PRIu32, minutes, seconds);
    lv_label_set_text(timer_label, time_str);
}

// 更新状态显示
static void update_state_display()
{
    const char *state_text = "";
    lv_color_t text_color = lv_color_hex(0xFFFFFF);

    switch (tomato_clock.state)
    {
    case TOMATO_STATE_IDLE:
        state_text = "就绪";
        text_color = lv_color_hex(0x808080);
        break;
    case TOMATO_STATE_WORKING:
        state_text = "工作中";
        text_color = lv_color_hex(0xFF5555);
        break;
    case TOMATO_STATE_BREAK:
        state_text = "休息";
        text_color = lv_color_hex(0x55FF55);
        break;
    }

    lv_label_set_text(state_label, state_text);
    lv_obj_set_style_text_color(state_label, text_color, 0);
}

// 开始/停止番茄时钟
static void start_stop_tomato()
{
    if (tomato_clock.is_running)
    {
        // 停止计时
        tomato_clock.is_running = false;
        lv_label_set_text(lv_obj_get_child(start_stop_btn, 0), "开始");
    }
    else
    {
        // 开始计时
        tomato_clock.is_running = true;
        lv_label_set_text(lv_obj_get_child(start_stop_btn, 0), "停止");

        // 如果是从空闲状态开始，设置为工作状态
        if (tomato_clock.state == TOMATO_STATE_IDLE)
        {
            tomato_clock.state = TOMATO_STATE_WORKING;
            tomato_clock.remaining_time = tomato_clock.work_duration;
            update_state_display();
        }
    }
}

// 重置番茄时钟
static void reset_tomato()
{
    tomato_clock.is_running = false;
    tomato_clock.state = TOMATO_STATE_IDLE;
    tomato_clock.remaining_time = tomato_clock.work_duration;

    lv_label_set_text(lv_obj_get_child(start_stop_btn, 0), "开始");
    update_timer_display();
    update_state_display();
}

// 每秒钟更新番茄时钟
static void update_tomato_clock()
{
    if (tomato_clock.is_running && tomato_clock.remaining_time > 0)
    {
        tomato_clock.remaining_time--;
        update_timer_display();

        // 检查是否需要切换状态
        if (tomato_clock.remaining_time == 0)
        {
            if (tomato_clock.state == TOMATO_STATE_WORKING)
            {
                // 工作时间结束，切换到休息时间
                tomato_clock.state = TOMATO_STATE_BREAK;
                tomato_clock.remaining_time = tomato_clock.break_duration;
            }
            else
            {
                // 休息时间结束，切换到工作时间
                tomato_clock.state = TOMATO_STATE_WORKING;
                tomato_clock.remaining_time = tomato_clock.work_duration;
            }
            update_state_display();
        }
    }
}

// 番茄时钟应用任务
void tomato_clock_task(void *arg)
{
    int key_press_num = 0;
    uint32_t last_update_time = 0;

    while (1)
    {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        // 每秒更新一次番茄时钟
        if (current_time - last_update_time >= 1000)
        {
            update_tomato_clock();
            last_update_time = current_time;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// 开始/停止按钮事件处理
static void start_stop_btn_handler(lv_event_t *e)
{
    start_stop_tomato();
}

// 重置按钮事件处理
static void reset_btn_handler(lv_event_t *e)
{
    reset_tomato();
}

static void button_chage_handle(lv_event_t *e)
{
        // 检查是否为点击事件
    if (e->code == LV_EVENT_CLICKED)
    {
         tomato_clock.work_duration += 10*60;
         if(tomato_clock.work_duration >= 100*60 ){
            tomato_clock.work_duration = 10*60;
         }
         reset_tomato();
    }

}

static void button_tomatoColock_to_home_handle(lv_event_t *e)
{
    // 检查是否为点击事件
    if (e->code == LV_EVENT_CLICKED)
    {
        // 停止任务
        vTaskDelete(app_tomato_handle);
        app_tomato_handle = NULL;
        lv_obj_del(app_cont); // 删除应用界面
        app_to_home_prepare();
    }
}

void button_home_to_tomatoColock_handle(lv_event_t *e)
{
    home_to_app_prepare(); // 隐藏home主界面

    // 创建应用界面容器
    app_cont = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(app_cont);
    lv_obj_set_size(app_cont, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_bg_color(app_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(app_cont, LV_OPA_COVER, 0);

    // 添加状态标签
    state_label = lv_label_create(app_cont);
    lv_obj_set_style_text_font(state_label, &font_alipuhui20, 0);
    lv_obj_align(state_label, LV_ALIGN_TOP_MID, 0, 40);

    // 添加计时器标签
    timer_label = lv_label_create(app_cont);
    lv_obj_set_style_text_font(timer_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(timer_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(timer_label, LV_ALIGN_CENTER, 0, -20);

    // 添加开始/停止按钮
    start_stop_btn = lv_btn_create(app_cont);
    lv_obj_set_size(start_stop_btn, 120, 50);
    lv_obj_align(start_stop_btn, LV_ALIGN_BOTTOM_MID, -70, -30);
    lv_obj_set_style_bg_color(start_stop_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(start_stop_btn, 10, 0);

    lv_obj_t *start_stop_label = lv_label_create(start_stop_btn);
    lv_label_set_text(start_stop_label, "开始");
    lv_obj_set_style_text_font(start_stop_label, &font_alipuhui20, 0);
    lv_obj_center(start_stop_label);
    lv_obj_add_event_cb(start_stop_btn, start_stop_btn_handler, LV_EVENT_CLICKED, NULL);

    // 添加重置按钮 // 全局变量
    reset_btn = lv_btn_create(app_cont);
    lv_obj_set_size(reset_btn, 120, 50);
    lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_MID, 70, -30);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(reset_btn, 10, 0);
    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "重置");
    lv_obj_set_style_text_font(reset_label, &font_alipuhui20, 0);
    lv_obj_center(reset_label);
    lv_obj_add_event_cb(reset_btn, reset_btn_handler, LV_EVENT_CLICKED, NULL);

    // 添加返回按钮（左上角小尺寸）
    return_btn = lv_btn_create(app_cont);
    lv_obj_set_size(return_btn, 60, 30);
    lv_obj_align(return_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(return_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(return_btn, 15, 0);
    lv_obj_t *btn_label = lv_label_create(return_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_label);
    // 添加按压效果
    lv_obj_set_style_transform_width(return_btn, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(return_btn, -2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(return_btn, lv_color_hex(0x555555), LV_STATE_PRESSED);
    // 添加事件回调
    lv_obj_add_event_cb(return_btn, button_tomatoColock_to_home_handle, LV_EVENT_CLICKED, NULL);

    // 添加切换计时按钮（右上角小尺寸）
    chage_btn = lv_btn_create(app_cont);
    lv_obj_set_size(chage_btn, 120, 30);
    lv_obj_align(chage_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(chage_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(chage_btn, 15, 0);
    lv_obj_t *chage_label = lv_label_create(chage_btn);
    lv_label_set_text(chage_label, "切换计时");
    lv_obj_set_style_text_font(chage_label, &font_alipuhui20, 0);
    lv_obj_center(chage_label);
    lv_obj_add_event_cb(chage_label, button_chage_handle, LV_EVENT_CLICKED, NULL);
    // 添加按压效果
    lv_obj_set_style_transform_width(chage_btn, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(chage_btn, -2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(chage_btn, lv_color_hex(0x555555), LV_STATE_PRESSED);
    // 添加事件回调
    lv_obj_add_event_cb(chage_btn, button_chage_handle, LV_EVENT_CLICKED, NULL);

    // 初始化显示
    update_timer_display();
    update_state_display();

    // 创建任务
    if (app_tomato_handle == NULL)
    {
        xTaskCreate(tomato_clock_task, "tomato_clock_task", 1024 * 4, NULL, PRI_APP, &app_tomato_handle);
    }
}
