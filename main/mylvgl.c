/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
// freertos
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// esp system
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_ft5x06.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
// user
#include "app.h"
#include "mylvgl.h"
#include "sysinfo.h"

// 系统状态资源
extern sys_info_t sys_info;
extern sys_resource_t sys_resource;

static const char *TAG = "LCD";
LV_IMG_DECLARE(rabi);
LV_IMG_DECLARE(icon_boot);
LV_IMG_DECLARE(booting);
LV_IMG_DECLARE(img_bilibili120);
LV_FONT_DECLARE(font_alipuhui20);
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Using SPI2 in the example
#define LCD_HOST SPI2_HOST
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 0
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_SCLK 3
#define EXAMPLE_PIN_NUM_MOSI 5
#define EXAMPLE_PIN_NUM_MISO -1
#define EXAMPLE_PIN_NUM_LCD_DC 6
#define EXAMPLE_PIN_NUM_LCD_RST -1
#define EXAMPLE_PIN_NUM_LCD_CS 4
#define EXAMPLE_PIN_NUM_BK_LIGHT 2
#define EXAMPLE_PIN_NUM_TOUCH_CS -1
#define EXAMPLE_LCD_H_RES 320
#define EXAMPLE_LCD_V_RES 240
#define EXAMPLE_LCD_CMD_BITS 8
#define EXAMPLE_LCD_PARAM_BITS 8
#define EXAMPLE_LVGL_TICK_PERIOD_MS 2

// #if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
// esp_lcd_touch_handle_t tp = NULL;
// #endif

/**************************** LCD背光调节初始化函数 **********************************/
static void lcd_brightness_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT, // Set duty resolution to 13 bits,
        .freq_hz = 5000,                      // Frequency in Hertz. Set frequency at 5 kHz
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = EXAMPLE_PIN_NUM_BK_LIGHT,
        .duty = 0, // Set duty
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

/**************************** LVGL相关处理函数 **********************************/
static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;

    /* Read touch controller data */
    esp_lcd_touch_read_data(drv->user_data);

    /* Get coordinates */
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(drv->user_data, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

    if (touchpad_pressed && touchpad_cnt > 0)
    {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void booting_task(void *arg)
{
    while (1)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int recovery_press_num = 0;
lv_event_cb_t recovery_btn_handle(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        recovery_press_num += 1;
        if (recovery_press_num >= 10) // 按下10次后进入擦除nvs，并重启
        {
            ESP_LOGI(TAG, "rec nvs!");
            ESP_ERROR_CHECK(nvs_flash_erase());
            ESP_LOGI(TAG, "reboot!");
            vTaskDelay(1000 / portTICK_PERIOD_MS); // 延迟1秒
            esp_restart();                         // 重启设备
        }
    }
}

void lvgl_init_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    // i2c在es8311处先初始化了
    // 等待i2初始化完成
    uint32_t notify_i2c_value = 0;
    while (notify_i2c_value != 1)
    {
        ESP_LOGI(TAG, "wait i2c init");
        if (xTaskNotifyWait(0, 0, &notify_i2c_value, pdMS_TO_TICKS(10000)) == pdTRUE)
        {
            if (notify_i2c_value == 1)
            {
                ESP_LOGI(TAG, "i2c init done. Value: %lu", notify_i2c_value);
                ESP_LOGI(TAG, "start lvgl init");
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /************* 初始化液晶屏  ************/
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;

    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_SCLK,
        .mosi_io_num = EXAMPLE_PIN_NUM_MOSI,
        .miso_io_num = EXAMPLE_PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EXAMPLE_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_PIN_NUM_LCD_DC,
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = example_notify_lvgl_flush_ready,
        .user_ctx = &disp_drv,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    ESP_LOGI(TAG, "Install ST7789 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /************* 初始化触摸屏 **************/
    esp_lcd_touch_handle_t tp = NULL;
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_V_RES,
        .y_max = EXAMPLE_LCD_H_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags = {
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };

    ESP_LOGI(TAG, "Initialize touch controller FT6336");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp));

    /************ 初始化LVGL *************/
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    lv_color_t *buf1 = heap_caps_malloc(EXAMPLE_LCD_H_RES * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);
    lv_color_t *buf2 = heap_caps_malloc(EXAMPLE_LCD_H_RES * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2);

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * 20);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    static lv_indev_drv_t indev_drv; // Input device driver (Touch)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_lvgl_touch_cb;
    indev_drv.user_data = tp;

    lv_indev_drv_register(&indev_drv);

    /******************* 初始化液晶屏背光 **********************/
    lcd_brightness_init();
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 8191 * (1 - 0.5))); // 设置占空比 0.5表示占空比是50%
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));                // 更新背光

    // 更新系统信息
    sys_resource.disp = disp;
    ESP_LOGI(TAG, "lvgl init done");

    // ------------------------------------------- 等待连接wifi---------------------------------
    // 添加配网设置按钮（左上角小尺寸）
    lv_obj_t *recovery_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(recovery_btn, 300, 30);                // 缩小按钮尺寸（宽度60，高度30）
    lv_obj_align(recovery_btn, LV_ALIGN_TOP_LEFT, 10, 10); // 对齐到左上角，带10px偏移
    lv_obj_set_style_bg_color(recovery_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(recovery_btn, 15, 0); // 圆角半径改为15（保持圆形比例）
    // 使用更小的字体
    lv_obj_t *btn_label = lv_label_create(recovery_btn);
    lv_label_set_text(btn_label, "擦除nvs,重新进入配网(点击10次)");       // 使用左箭头符号替代文字
    lv_obj_set_style_text_font(btn_label, &font_alipuhui20, 0); //
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_label);
    // 添加按压效果
    lv_obj_set_style_transform_width(recovery_btn, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(recovery_btn, -2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(recovery_btn, lv_color_hex(0x555555), LV_STATE_PRESSED);
    // 添加事件回调
    lv_obj_add_event_cb(recovery_btn, recovery_btn_handle, LV_EVENT_CLICKED, NULL);

    // 显示开机图片
    lv_obj_t *boot_start = lv_gif_create(lv_scr_act());
    lv_gif_set_src(boot_start, &rabi);
    // lv_obj_set_size(boot_start, 150, 150);
    lv_obj_align(boot_start, LV_ALIGN_CENTER, 0, -15);
    // 显示wifi连接信息
    lv_obj_t *label_wifi_info = lv_label_create(lv_scr_act());
    lv_obj_align(label_wifi_info, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_set_style_text_font(label_wifi_info, &font_alipuhui20, LV_STATE_DEFAULT);
    lv_label_set_text(label_wifi_info, "正在连接wifi...");
    lv_timer_handler();

    TaskHandle_t booting_handle;
    xTaskCreate(booting_task, "booting_task", 1024 * 4, NULL, PRI_APP, &booting_handle); // 一次性任务

    // 等待wifi连接完成
    uint32_t notify_value = 0;
    while (notify_value != 2)
    {
        if (xTaskNotifyWait(0, 0, &notify_value, pdMS_TO_TICKS(portMAX_DELAY)) == pdTRUE)
        {
            if (notify_value == 2)
            {
                ESP_LOGI(TAG, "wifi init done. Value: %lu", notify_value);
                ESP_LOGI(TAG, "start home page");
                vTaskDelay(pdMS_TO_TICKS(1000)); // 延时
                                                 // 删除对象
                if (boot_start)
                {
                    lv_obj_del(boot_start); // 从屏幕上移除并释放内存
                    boot_start = NULL;      // 防止悬空指针
                }
                // 删除 WiFi 相关信息标签
                if (label_wifi_info)
                {
                    lv_obj_del(label_wifi_info); // 从屏幕上移除并释放内存
                    label_wifi_info = NULL;      // 防止悬空指针
                }
                if (recovery_btn)
                {
                    lv_obj_del(recovery_btn);
                    recovery_btn = NULL;
                }
                vTaskDelete(booting_handle);
                booting_handle = NULL;
                app_home();
            }
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    while (1)
    {

        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(20));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
    }
}
