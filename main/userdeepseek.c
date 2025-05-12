#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "sdkconfig.h"
#include "audio_common.h"
#include "audio_hal.h"
#include "userdeepseek.h"
#include "cJSON.h"
#include "sysinfo.h"

// 外部资源
extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例

// 本文件资源
static const char *TAG = "MINIMAX_CHAT";
#define MAX_CHAT_BUFFER (2048)
char deepseek_content[2048] = {0};

char *deepseek_chat(const char *text)
{
    char *response_text = NULL;
    char *post_buffer = NULL;
    char *data_buf = NULL;

    esp_http_client_config_t config = {
        .url = sys_info.deepseek_url, // 填写DeepSeek API地址
        .buffer_size_tx = 1024        // 保持1024缓冲区大小
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

#define DEEPSEEK_POST_DATA "{\
        \"model\": \"%s\",\
        \"messages\": [\
            {\"role\": \"system\", \"content\": \"%s\"},\
            {\"role\": \"user\", \"content\": \"%s\"}\
        ],\
        \"stream\": false,\
        \"max_tokens\": 2048,\
        \"temperature\": 1.3\
    }" // 新增system角色消息和stream参数 \"max_tokens\": 50,\  \"temperature\": 0.3\
    
    // 动态合成post内容
    int post_len = asprintf(&post_buffer, DEEPSEEK_POST_DATA, sys_info.deepseek_model, sys_info.deepseek_tipword, text);

    if (post_buffer == NULL)
    {
        goto exit_translate;
    }

    // 设置HTTP头// Bearer认证方式 api key 前面添加 Bearer 字样
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", sys_info.deepseek_key); 

    if (esp_http_client_open(client, post_len) != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening connection");
        goto exit_translate;
    }

    int write_len = esp_http_client_write(client, post_buffer, post_len);
    ESP_LOGI(TAG, "Need to write %d, written %d", post_len, write_len);

    int count = 0;
    int data_length = esp_http_client_fetch_headers(client); // 获取响应头
    while (data_length == 0 && count <= 10)
    { // d
        printf("data_length = %d\n", data_length);
        data_length = esp_http_client_fetch_headers(client); // 获取响应头
                                                             // count++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("data_length = %d\n", data_length);
    if (data_length <= 0)
    {
        data_length = MAX_CHAT_BUFFER;
    }

    data_buf = malloc(data_length + 1);
    if (data_buf == NULL)
    {
        goto exit_translate;
    }
    data_buf[data_length] = '\0';

    int rlen = esp_http_client_read(client, data_buf, data_length);
    data_buf[rlen] = '\0';
    ESP_LOGI(TAG, "read = %s", data_buf);

    // 解析DeepSeek的响应格式
    cJSON *root = cJSON_Parse(data_buf);
    if (root)
    {
        cJSON *choices = cJSON_GetObjectItem(root, "choices");
        if (choices && cJSON_GetArraySize(choices) > 0)
        {
            cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
            cJSON *message = cJSON_GetObjectItem(first_choice, "message");
            char *reply = cJSON_GetObjectItem(message, "content")->valuestring;

            strncpy(deepseek_content, reply, sizeof(deepseek_content) - 1);
            response_text = deepseek_content;
            ESP_LOGI(TAG, "Response: %s", response_text);
        }
        cJSON_Delete(root);
    }

exit_translate:
    // 返回前销毁资源
    free(post_buffer);
    free(data_buf);
    esp_http_client_cleanup(client);
    return response_text;
}
