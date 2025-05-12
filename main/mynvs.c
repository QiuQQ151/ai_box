#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
// freertos
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// esp_system
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
//
#include "mywifi.h"
//
#include "usersntp.h"
#include "usertask.h"
#include "sysinfo.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "lwip/ip4_addr.h"
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sysinfo.h"
extern sys_info_t sys_info;         // 系统状态信息结构体实例
extern sys_resource_t sys_resource; // 系统资源接口结构体实例

static const char *TAG = "NVS";

// HTTP状态码定义
#define HTTPD_413_ENTITY_TOO_LARGE 413

// SoftAP配置
#define SOFTAP_SSID "AI-BOX(pass:12345678)"
#define SOFTAP_PASSWORD "12345678"
#define SOFTAP_CHANNEL 6
#define MAX_STA_CONN 4

// NVS键定义
#define WIFI_CONFIG_NVS_KEY "wifi_config"
#define WIFI_SSID_KEY "ssid"
#define WIFI_PASS_KEY "password"

// 新增配置项键定义
#define BAIDU_ACCESS_KEY "baidu_key"
#define BAIDU_SECRET_KEY "baidu_secret"
#define DEEPSEEK_API_KEY "deepseek_key"
#define DEEPSEEK_API_URL "deepseek_url"
#define MODEL_NAME "model"
#define TIP_WORD "tipword"

// 默认值
#define DEFAULT_BAIDU_ACCESS_KEY "your_baidu_key"
#define DEFAULT_BAIDU_SECRET_KEY "your_baidu_secret"
#define DEFAULT_DEEPSEEK_API_KEY "Bearer your_deepseek_key"
#define DEFAULT_DEEPSEEK_API_URL "https://api.deepseek.com/v1"
#define DEFAULT_MODEL "default_model"
#define DEFAULT_TIPWORD "你好，我是ESP32-C3助手"

static httpd_handle_t server = NULL;

// WiFi事件处理
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_AP_STACONNECTED)
        {
            ESP_LOGI(TAG, "设备连接");
        }
        else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
        {
            ESP_LOGI(TAG, "设备断开");
        }
    }
}

// HTML配置页面
static const char *CONFIG_HTML =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>ESP32-C3 配置</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }"
    "h1 { color: #333; }"
    "input, textarea { width: 100%; padding: 8px; margin: 5px 0; box-sizing: border-box; }"
    "button { padding: 10px; background: #4CAF50; color: white; border: none; width: 100%; }"
    ".section { margin-bottom: 15px; border: 1px solid #ddd; padding: 10px; border-radius: 5px; }"
    ".section-title { font-weight: bold; margin-bottom: 5px; }"
    "</style>"
    "</head>"
    "<body>"
    "<h1>ESP32-C3 配置</h1>"
    "<form action=\"/save\" method=\"post\">"
    "<div class=\"section\">"
    "<div class=\"section-title\">WiFi 配置</div>"
    "<input type=\"text\" name=\"ssid\" placeholder=\"WiFi名称\" required><br>"
    "<input type=\"password\" name=\"password\" placeholder=\"WiFi密码\" required><br>"
    "</div>"

    "<div class=\"section\">"
    "<div class=\"section-title\">百度API配置</div>"
    "<input type=\"text\" name=\"baidu_key\" placeholder=\"百度API Key\" required><br>"
    "<input type=\"text\" name=\"baidu_secret\" placeholder=\"百度Secret Key\" required><br>"
    "</div>"

    "<div class=\"section\">"
    "<div class=\"section-title\">DeepSeek配置</div>"
    "<input type=\"text\" name=\"deepseek_key\" placeholder=\"DeepSeek API Key\" required><br>"
    "<input type=\"text\" name=\"deepseek_url\" placeholder=\"API URL\" required><br>"
    "<input type=\"text\" name=\"model\" placeholder=\"模型名称\" required><br>"
    "</div>"

    "<div class=\"section\">"
    "<div class=\"section-title\">提示词配置</div>"
    "<textarea name=\"tipword\" rows=\"4\" placeholder=\"系统提示词\" required></textarea><br>"
    "</div>"

    "<button type=\"submit\">保存配置</button>"
    "</form>"
    "</body>"
    "</html>";

// 发送html页面数据
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html"); // 
    httpd_resp_send(req, CONFIG_HTML, strlen(CONFIG_HTML)); //
    return ESP_OK;
}

static void save_default_configs(nvs_handle_t nvs)
{
    ESP_ERROR_CHECK(nvs_set_str(nvs, BAIDU_ACCESS_KEY, DEFAULT_BAIDU_ACCESS_KEY));
    ESP_ERROR_CHECK(nvs_set_str(nvs, BAIDU_SECRET_KEY, DEFAULT_BAIDU_SECRET_KEY));
    ESP_ERROR_CHECK(nvs_set_str(nvs, DEEPSEEK_API_KEY, DEFAULT_DEEPSEEK_API_KEY));
    ESP_ERROR_CHECK(nvs_set_str(nvs, DEEPSEEK_API_URL, DEFAULT_DEEPSEEK_API_URL));
    ESP_ERROR_CHECK(nvs_set_str(nvs, MODEL_NAME, DEFAULT_MODEL));
    ESP_ERROR_CHECK(nvs_set_str(nvs, TIP_WORD, DEFAULT_TIPWORD));
    ESP_ERROR_CHECK(nvs_commit(nvs));
}

static int find_string_param(char *content, const char *param_name, char *output, size_t max_len)
{
    char search_str[64];
    snprintf(search_str, sizeof(search_str), "%s=", param_name);
    char *param_start = strstr(content, search_str);
    if (!param_start)
        return 0;

    param_start += strlen(search_str);
    char *param_end = strchr(param_start, '&');
    if (!param_end)
        param_end = param_start + strlen(param_start);

    size_t len = param_end - param_start;
    if (len == 0)
        return 0;

    size_t decoded_len = 0;
    for (char *p = param_start; p < param_end;)
    {
        if (*p == '+')
        {
            output[decoded_len++] = ' ';
            p++;
        }
        else if (*p == '%' && p + 2 < param_end &&
                 isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2]))
        { // 修复点1
            char hex[3] = {p[1], p[2], '\0'};
            output[decoded_len++] = (char)strtoul(hex, NULL, 16);
            p += 3;
        }
        else
        {
            output[decoded_len++] = *p;
            p++;
        }

        if (decoded_len >= max_len - 1)
            break;
    }

    output[decoded_len] = '\0';
    return decoded_len;
}

static esp_err_t save_post_handler(httpd_req_t *req) // 处理保存配置的POST请求
{
    char content[4096]; // 用于存储接收到的请求内容
    int ret, remaining = req->content_len; // 获取请求内容的长度

    if (remaining >= sizeof(content)) // 如果请求内容超过缓冲区大小
    {
        ESP_LOGE(TAG, "请求内容太大"); // 打印错误日志
        httpd_resp_send_err(req, HTTPD_413_ENTITY_TOO_LARGE, "请求内容太大"); // 返回413错误
        return ESP_FAIL; // 返回失败
    }

    int received = 0; // 已接收的字节数
    while (remaining > 0) // 循环接收请求内容
    {
        ret = httpd_req_recv(req, content + received, remaining); // 接收数据
        if (ret <= 0) // 如果接收失败
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) // 如果是超时错误
                continue; // 继续接收
            ESP_LOGE(TAG, "接收数据失败"); // 打印错误日志
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "接收数据失败"); // 返回500错误
            return ESP_FAIL; // 返回失败
        }
        received += ret; // 更新已接收的字节数
        remaining -= ret; // 更新剩余字节数
    }
    content[received] = '\0'; // 添加字符串结束符

    // 准备存储所有配置数据
    char ssid[64] = {0}; // WiFi名称
    char password[128] = {0}; // WiFi密码
    char baidu_key[256] = {0}; // 百度API Key
    char baidu_secret[256] = {0}; // 百度Secret Key
    char deepseek_key[256] = {0}; // DeepSeek API Key
    char deepseek_url[512] = {0}; // DeepSeek API URL
    char model[128] = {0}; // 模型名称
    char tipword[1024] = {0}; // 提示词

    // 解析所有参数
    int found = 0; // 记录找到的参数数量
    found += find_string_param(content, "ssid", ssid, sizeof(ssid)); // 解析WiFi名称
    found += find_string_param(content, "password", password, sizeof(password)); // 解析WiFi密码
    found += find_string_param(content, "baidu_key", baidu_key, sizeof(baidu_key)); // 解析百度API Key
    found += find_string_param(content, "baidu_secret", baidu_secret, sizeof(baidu_secret)); // 解析百度Secret Key
    found += find_string_param(content, "deepseek_key", deepseek_key, sizeof(deepseek_key)); // 解析DeepSeek API Key
    found += find_string_param(content, "deepseek_url", deepseek_url, sizeof(deepseek_url)); // 解析DeepSeek API URL
    found += find_string_param(content, "model", model, sizeof(model)); // 解析模型名称
    found += find_string_param(content, "tipword", tipword, sizeof(tipword)); // 解析提示词

    if (found > 0) // 如果找到至少一个参数
    {
        nvs_handle_t nvs; // 定义NVS句柄
        ESP_ERROR_CHECK(nvs_open(WIFI_CONFIG_NVS_KEY, NVS_READWRITE, &nvs)); // 打开NVS存储空间

        if (ssid[0]) // 如果WiFi名称不为空
            ESP_ERROR_CHECK(nvs_set_str(nvs, WIFI_SSID_KEY, ssid)); // 保存WiFi名称
        if (password[0]) // 如果WiFi密码不为空
            ESP_ERROR_CHECK(nvs_set_str(nvs, WIFI_PASS_KEY, password)); // 保存WiFi密码
        if (baidu_key[0]) // 如果百度API Key不为空
            ESP_ERROR_CHECK(nvs_set_str(nvs, BAIDU_ACCESS_KEY, baidu_key)); // 保存百度API Key
        if (baidu_secret[0]) // 如果百度Secret Key不为空
            ESP_ERROR_CHECK(nvs_set_str(nvs, BAIDU_SECRET_KEY, baidu_secret)); // 保存百度Secret Key
        if (deepseek_key[0]) // 如果DeepSeek API Key不为空
            ESP_ERROR_CHECK(nvs_set_str(nvs, DEEPSEEK_API_KEY, deepseek_key)); // 保存DeepSeek API Key
        if (deepseek_url[0]) // 如果DeepSeek API URL不为空
            ESP_ERROR_CHECK(nvs_set_str(nvs, DEEPSEEK_API_URL, deepseek_url)); // 保存DeepSeek API URL
        if (model[0]) // 如果模型名称不为空
            ESP_ERROR_CHECK(nvs_set_str(nvs, MODEL_NAME, model)); // 保存模型名称
        if (tipword[0]) // 如果提示词不为空
            ESP_ERROR_CHECK(nvs_set_str(nvs, TIP_WORD, tipword)); // 保存提示词

        ESP_ERROR_CHECK(nvs_commit(nvs)); // 提交NVS更改
        nvs_close(nvs); // 关闭NVS存储空间

        ESP_LOGI(TAG, "配置保存成功:"); // 打印日志
        ESP_LOGI(TAG, "- WiFi: %s / %s", ssid, password); // 打印WiFi配置
        ESP_LOGI(TAG, "- 百度: %s / %s", baidu_key, baidu_secret); // 打印百度配置
        ESP_LOGI(TAG, "- DeepSeek: %s / %s / %s", deepseek_key, deepseek_url, model); // 打印DeepSeek配置
        ESP_LOGI(TAG, "- 提示词: %s", tipword); // 打印提示词

        const char *success = // 定义成功页面HTML内容
            "<html><body style='font-family:Arial;padding:20px;'>"
            "<h1>配置保存成功!</h1>"
            "<p>设备将重启...</p>"
            "<script>setTimeout(function(){ window.location='/'; }, 3000);</script>"
            "</body></html>";

        httpd_resp_set_type(req, "text/html"); // 设置响应类型为HTML
        httpd_resp_send(req, success, strlen(success)); // 发送成功页面

        vTaskDelay(1000 / portTICK_PERIOD_MS); // 延迟1秒
        esp_restart(); // 重启设备
    }
    else // 如果没有找到任何参数
    {
        httpd_resp_send_404(req); // 返回404错误
        return ESP_FAIL; // 返回失败
    }

    return ESP_OK; // 返回成功
}

typedef struct
{
    char ssid[64];
    char password[128];
    char baidu_key[256];
    char baidu_secret[256];
    char deepseek_key[256];
    char deepseek_url[512];
    char model[128];
    char tipword[1024];
} sys_config_t;

// 加载nvs中的所有存储配置信息
static bool load_all_configs(sys_config_t *config)
{
    nvs_handle_t nvs;
    if (nvs_open(WIFI_CONFIG_NVS_KEY, NVS_READONLY, &nvs) != ESP_OK) // 打开NVS存储空间，若失败返回false
    {
        return false;
    }

    memset(config, 0, sizeof(sys_config_t)); // 清空配置结构体

    size_t len = sizeof(config->ssid);
    if (nvs_get_str(nvs, WIFI_SSID_KEY, config->ssid, &len) != ESP_OK) // 从NVS中读取WiFi SSID
    {
        nvs_close(nvs); // 若读取失败，关闭NVS并返回false
        return false;
    }

    len = sizeof(config->password);
    if (nvs_get_str(nvs, WIFI_PASS_KEY, config->password, &len) != ESP_OK) // 从NVS中读取WiFi密码
    {
        nvs_close(nvs); // 若读取失败，关闭NVS并返回false
        return false;
    }

    len = sizeof(config->baidu_key);
    if (nvs_get_str(nvs, BAIDU_ACCESS_KEY, config->baidu_key, &len) != ESP_OK) // 从NVS中读取百度API Key
    {
        strncpy(config->baidu_key, DEFAULT_BAIDU_ACCESS_KEY, sizeof(config->baidu_key) - 1); // 若读取失败，使用默认值
    }

    len = sizeof(config->baidu_secret);
    if (nvs_get_str(nvs, BAIDU_SECRET_KEY, config->baidu_secret, &len) != ESP_OK) // 从NVS中读取百度Secret Key
    {
        strncpy(config->baidu_secret, DEFAULT_BAIDU_SECRET_KEY, sizeof(config->baidu_secret) - 1); // 若读取失败，使用默认值
    }

    len = sizeof(config->deepseek_key);
    if (nvs_get_str(nvs, DEEPSEEK_API_KEY, config->deepseek_key, &len) != ESP_OK) // 从NVS中读取DeepSeek API Key
    {
        strncpy(config->deepseek_key, DEFAULT_DEEPSEEK_API_KEY, sizeof(config->deepseek_key) - 1); // 若读取失败，使用默认值
    }

    len = sizeof(config->deepseek_url);
    if (nvs_get_str(nvs, DEEPSEEK_API_URL, config->deepseek_url, &len) != ESP_OK) // 从NVS中读取DeepSeek API URL
    {
        strncpy(config->deepseek_url, DEFAULT_DEEPSEEK_API_URL, sizeof(config->deepseek_url) - 1); // 若读取失败，使用默认值
    }

    len = sizeof(config->model);
    if (nvs_get_str(nvs, MODEL_NAME, config->model, &len) != ESP_OK) // 从NVS中读取模型名称
    {
        strncpy(config->model, DEFAULT_MODEL, sizeof(config->model) - 1); // 若读取失败，使用默认值
    }

    len = sizeof(config->tipword);
    if (nvs_get_str(nvs, TIP_WORD, config->tipword, &len) != ESP_OK) // 从NVS中读取提示词
    {
        strncpy(config->tipword, DEFAULT_TIPWORD, sizeof(config->tipword) - 1); // 若读取失败，使用默认值
    }

    nvs_close(nvs); // 关闭NVS存储空间
    return true; // 返回true表示加载成功
}

// 处理favicon.ico请求的HTTP处理程序
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content"); // 设置HTTP响应状态为204，无内容
    httpd_resp_send(req, NULL, 0); // 发送空的响应内容
    return ESP_OK; // 返回ESP_OK表示处理成功
}

static void register_file_handlers(httpd_handle_t server)
{
    // 定义根路径的URI处理程序
    static const httpd_uri_t root = {
        .uri = "/",                 // URI路径为"/"
        .method = HTTP_GET,         // 请求方法为GET
        .handler = root_get_handler, // 处理函数为root_get_handler
        .user_ctx = NULL};          // 用户上下文为空

    // 定义保存配置的URI处理程序
    static const httpd_uri_t save = {
        .uri = "/save",             // URI路径为"/save"
        .method = HTTP_POST,        // 请求方法为POST
        .handler = save_post_handler, // 处理函数为save_post_handler
        .user_ctx = NULL};          // 用户上下文为空

    // 定义favicon的URI处理程序
    httpd_uri_t favicon = {
        .uri = "/favicon.ico",      // URI路径为"/favicon.ico"
        .method = HTTP_GET,         // 请求方法为GET
        .handler = favicon_get_handler, // 处理函数为favicon_get_handler
    };

    // 注册根路径的URI处理程序
    httpd_register_uri_handler(server, &root);
    // 注册保存配置的URI处理程序
    httpd_register_uri_handler(server, &save);
    // 注册favicon的URI处理程序
    httpd_register_uri_handler(server, &favicon);
}

// 启动HTTP服务器
static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); // 获取默认的HTTP服务器配置
    config.server_port = 80; // 设置服务器端口为80
    config.max_uri_handlers = 8; // 设置最大URI处理程序数量为8
    config.stack_size = 10240; // 设置HTTP服务器任务的堆栈大小为10240字节

    // 尝试启动HTTP服务器
    if (httpd_start(&server, &config) == ESP_OK)
    {
        register_file_handlers(server); // 注册URI处理程序
        ESP_LOGI(TAG, "HTTP服务器启动成功"); // 打印日志，表示服务器启动成功
    }
    else
    {
        ESP_LOGE(TAG, "HTTP服务器启动失败"); // 打印日志，表示服务器启动失败
    }
}

void wifi_init_softap(void)
{
    esp_netif_create_default_wifi_ap(); // 创建默认的WiFi AP网络接口

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // 初始化WiFi配置
    ESP_ERROR_CHECK(esp_wifi_init(&cfg)); // 初始化WiFi驱动

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL)); // 注册WiFi事件处理程序

    wifi_config_t wifi_config = { // 配置SoftAP模式的WiFi参数
        .ap = {
            .ssid = SOFTAP_SSID, // 设置SoftAP的SSID
            .password = SOFTAP_PASSWORD, // 设置SoftAP的密码
            .ssid_len = strlen(SOFTAP_SSID), // 设置SSID长度
            .channel = SOFTAP_CHANNEL, // 设置SoftAP的信道
            .max_connection = MAX_STA_CONN, // 设置最大连接数
            .authmode = WIFI_AUTH_WPA2_PSK}}; // 设置认证模式为WPA2-PSK
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP)); // 设置WiFi模式为AP
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config)); // 应用SoftAP配置
    ESP_ERROR_CHECK(esp_wifi_start()); // 启动WiFi AP模式

    esp_netif_ip_info_t ip_info; // 定义IP信息结构体
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1); // 设置IP地址为192.168.4.1
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1); // 设置网关地址为192.168.4.1
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0); // 设置子网掩码为255.255.255.0

    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"); // 获取AP网络接口句柄
    esp_netif_dhcps_stop(ap_netif); // 停止DHCP服务器
    esp_netif_set_ip_info(ap_netif, &ip_info); // 设置IP信息
    esp_netif_dhcps_start(ap_netif); // 启动DHCP服务器

    ESP_LOGI(TAG, "SoftAP已启动"); // 打印日志，表示SoftAP已启动
}


// nvs初始化任务
void nvs_init_task(void *arg)
{
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) // 若初始化失败，擦除nvs
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized successfully!");
    ESP_ERROR_CHECK(esp_netif_init()); // 初始化网络接口

    // 检查是否有WiFi配置
    sys_config_t config;
    bool has_config = load_all_configs(&config); // 从nvs加载信息
    if (!has_config || strlen(config.ssid) == 0)  // 无法加载或wifi信息出错
    {
        ESP_ERROR_CHECK(esp_event_loop_create_default());  
        ESP_LOGI(TAG, "进入配置模式");
        wifi_init_softap();  // 启动ap模式
        start_webserver();   // 开启网页服务

        // 如果没有任何配置，保存默认值
        nvs_handle_t nvs;
        if (nvs_open(WIFI_CONFIG_NVS_KEY, NVS_READWRITE, &nvs) == ESP_OK)
        {
            save_default_configs(nvs);
            nvs_close(nvs);
        }
    }
    else
    {
        // 将系统配置保存进sys_info状态 // 安全复制
        snprintf(sys_info.ssid, sizeof(sys_info.ssid), "%s", config.ssid);                           
        snprintf(sys_info.password, sizeof(sys_info.password), "%s", config.password);                
        snprintf(sys_info.baidu_key, sizeof(sys_info.baidu_key), "%s", config.baidu_key);            
        snprintf(sys_info.baidu_secret, sizeof(sys_info.baidu_secret), "%s", config.baidu_secret);    
        snprintf(sys_info.deepseek_key, sizeof(sys_info.deepseek_key), "%s", config.deepseek_key);    
        snprintf(sys_info.deepseek_model, sizeof(sys_info.deepseek_model), "%s", config.model);      
        snprintf(sys_info.deepseek_tipword, sizeof(sys_info.deepseek_tipword), "%s", config.tipword); 
        snprintf(sys_info.deepseek_url, sizeof(sys_info.deepseek_url), "%s", config.deepseek_url);    
        sys_info.nvs_stata = true;

        ESP_LOGI(TAG, "已找到保存的配置");
        ESP_LOGI(TAG, "WiFi: %s", sys_info.ssid);
        ESP_LOGI(TAG, "PASS: %s", sys_info.password);
        ESP_LOGI(TAG, "百度Key: %s", sys_info.baidu_key);
        ESP_LOGI(TAG, "百度secret: %s", sys_info.baidu_secret);
        ESP_LOGI(TAG, "DeepSeek URL: %s", sys_info.deepseek_url);
        ESP_LOGI(TAG, "DeepSeek model: %s", sys_info.deepseek_model);
        ESP_LOGI(TAG, "DeepSeek key: %s", sys_info.deepseek_key);
        ESP_LOGI(TAG, "提示词: %s", sys_info.deepseek_tipword);

        // 通知wifi的任务
        TaskHandle_t task_handle = (TaskHandle_t)arg;
        xTaskNotify(task_handle, 1, eSetValueWithOverwrite);

        // 存在参数返回
        vTaskDelete(NULL);
    }

    while (1)
    {
        // 参数不存在，等待配置，保留任务
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}