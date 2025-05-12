#include "app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sysinfo.h"
extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例

static lv_obj_t *app_cont = NULL;   // 当前活动app应用容器
static lv_obj_t *return_btn = NULL; // 返回按钮

static TaskHandle_t app_test_handle = NULL; // 避免重复创建任务

void app_test_task(void *arg)
{
    int key_press_num = 0;
    while (1)
    {
       if (sys_resource.current_app_id == Test_APP_ID)
        {

        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static button_test_to_home_handle(lv_event_t *e)
{
    // 检查是否为点击事件
    if (e->code == LV_EVENT_CLICKED)
    {
        lv_obj_del(app_cont); // 删除应用界面
        app_to_home_prepare();
    }
}

void button_home_to_test_handler(lv_event_t *e)
{
    home_to_app_prepare(); // 隐藏home主界面
                           // 界面配置
    // 应用初始化
    // 创建应用界面容器
    app_cont = lv_obj_create(lv_scr_act()); // 在活动屏幕上创建
    lv_obj_remove_style_all(app_cont);
    lv_obj_set_size(app_cont, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_bg_color(app_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(app_cont, LV_OPA_COVER, 0);

    // 创建应用内容

    // 添加返回按钮（左上角小尺寸）
    return_btn = lv_btn_create(app_cont);
    lv_obj_set_size(return_btn, 60, 30);                 // 缩小按钮尺寸（宽度60，高度30）
    lv_obj_align(return_btn, LV_ALIGN_TOP_LEFT, 10, 10); // 对齐到左上角，带10px偏移
    lv_obj_set_style_bg_color(return_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(return_btn, 15, 0); // 圆角半径改为15（保持圆形比例）

    // 使用更小的字体
    lv_obj_t *btn_label = lv_label_create(return_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_LEFT);                     // 使用左箭头符号替代文字
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0); // 字体缩小到14px
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_label);

    // 添加按压效果
    lv_obj_set_style_transform_width(return_btn, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(return_btn, -2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(return_btn, lv_color_hex(0x555555), LV_STATE_PRESSED);

    // 添加事件回调
    lv_obj_add_event_cb(return_btn, button_test_to_home_handle, LV_EVENT_CLICKED, NULL);

    // 其它配置 使用任务更新
    if (app_test_handle == NULL)
    {
        xTaskCreate(app_test_task, "app_test_task", 1024 * 5, NULL, 10, &app_test_handle);
    }
}