# 硬件
- 对应嘉立创的esp32-c3开发板，aichat功能参考立创资料，大模型改为deepseek模型

# main文件夹
- my开头对应底层硬件
- user开头对应中间函数
- app开头对应实际应用
- 其余为字库或图片icon等

# 功能
- 联网配置信息
- aichat
- 番茄时钟
- 网络收音机

# main中cmakelist注意
尽量不使用
#set(COMPONENT_REQUIRES freertos esp_wifi esp_event esp_log esp_system esp_http_client lvgl lvgl_esp32_drivers lvgl_helpers lvgl_tft esp_http_server nvs_flash esp_adc_cal esp_timer esp_event driver esp_system)
#set(COMPONENT_PRIV_REQUIRES freertos)  # 仅内部使用，不强制检查外部依赖
避免显示声明引用，而是让IDF自动管理隐式传递调用，避免版本问题导致编译出错

# adf的安装使用
直接git clone 到目标文件夹，补充componets下的缺失，然后设置vscode路径

#　在include后提示函数缺失问题
查看sdkconfig中是否开启了相应的宏定义

