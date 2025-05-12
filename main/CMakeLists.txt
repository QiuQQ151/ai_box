
file(GLOB_RECURSE DEMO_SOURCES *.c)

set(COMPONENT_SRCS "icon_radio.c" 
                   "icon_clock.c" 
                   "icon_battle.c" 
                   "icon_deepseek.c"
                   "icon_boot.c"
                   "booting.c"
                   "app_home.c" 
                   "main.c"
                   "mynvs.c"
                   "mywifi.c"
                   "myes8311.c"
                   "mylvgl.c"
                   "mykey.c"
                   "usersntp.c" 
                   "usertask.c"
                   "userbaidu_tts.c"
                   "userbaidu_vtt.c"
                   "userdeepseek.c"
                   "rabi.c"
                   "img_bilibili120.c"
                   "font_alipuhui20.c"
                   "app_home.c"
                   "app_test.c"
                   "app_aiChat.c"
                   "app_tomatoClock.c"
                   "app_radio.c"
                #    "app_radio.c"
                #    "app_setting.c"
                #    "app_tempHumidity.c"
                #     "app_test.c"
                #    
                #    "app_battleKedi.c"
                   )
set(COMPONENT_ADD_INCLUDEDIRS .)
register_component()

# Some of warinings from LVGL. Block them.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)



