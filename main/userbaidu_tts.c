/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "audio_hal.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "amr_decoder.h"
#include "wav_decoder.h"
#include "pcm_decoder.h"
#include "userbaidu_tts.h"

static const char *TAG = "BAIDU_TTS";

extern char *baidu_access_token;

#define BAIDU_TTS_ENDPOINT "http://tsn.baidu.com/text2audio"
#define BAIDU_TTS_TASK_STACK (8 * 1024)

typedef struct baidu_tts
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t i2s_writer;
    audio_element_handle_t http_stream_reader;
    audio_element_handle_t mp3_decoder;
    int buffer_size;
    char *buffer;
    char *text;
    int sample_rate;
} baidu_tts_t;

static esp_err_t _http_stream_reader_event_handle(http_stream_event_msg_t *msg) // 定义HTTP流事件处理函数
{
    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client; // 获取HTTP客户端句柄
    baidu_tts_t *tts = (baidu_tts_t *)msg->user_data; // 获取用户数据，转换为baidu_tts_t类型

    if (msg->event_id == HTTP_STREAM_PRE_REQUEST) // 如果事件是HTTP_STREAM_PRE_REQUEST
    {
        // 发送文本数据
        ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_PRE_REQUEST, lenght=%d", msg->buffer_len); // 打印日志，显示缓冲区长度

        int payload_len = snprintf(tts->buffer, tts->buffer_size, "lan=zh&cuid=ESP32&ctp=1&vol=15&per=4&tok=%s&tex=%s", baidu_access_token, tts->text); // 格式化POST数据
        esp_http_client_set_post_field(http, tts->buffer, payload_len); // 设置HTTP POST字段
        esp_http_client_set_method(http, HTTP_METHOD_POST); // 设置HTTP方法为POST
        esp_http_client_set_header(http, "Content-Type", "application/x-www-form-urlencoded"); // 设置HTTP头，内容类型为表单
        esp_http_client_set_header(http, "Accept", "*/*"); // 设置HTTP头，接受所有类型

        return ESP_OK; // 返回成功
    }

    return ESP_OK; // 返回成功
}

baidu_tts_handle_t baidu_tts_init(baidu_tts_config_t *config)
{
    // 管道设置
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    baidu_tts_t *tts = calloc(1, sizeof(baidu_tts_t));
    AUDIO_MEM_CHECK(TAG, tts, return NULL);

    tts->pipeline = audio_pipeline_init(&pipeline_cfg);

    tts->buffer_size = config->buffer_size;
    if (tts->buffer_size <= 0)
    {
        tts->buffer_size = DEFAULT_TTS_BUFFER_SIZE;
    }

    tts->buffer = malloc(tts->buffer_size);
    AUDIO_MEM_CHECK(TAG, tts->buffer, goto exit_tts_init);

    tts->sample_rate = config->playback_sample_rate;

    // // I2S流设置
    // i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(0, 16000, 16, AUDIO_STREAM_WRITER);
    // i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    // i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    // tts->i2s_writer = i2s_stream_init(&i2s_cfg);

    // // http流设置
    // http_stream_cfg_t http_cfg = {
    //     .type = AUDIO_STREAM_READER,
    //     .event_handle = _http_stream_reader_event_handle,
    //     .user_data = tts,
    //     .task_stack = BAIDU_TTS_TASK_STACK,
    // };
    // tts->http_stream_reader = http_stream_init(&http_cfg);

    // // MP3流设置
    // mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    // tts->mp3_decoder = mp3_decoder_init(&mp3_cfg);

    // audio_pipeline_register(tts->pipeline, tts->http_stream_reader, "tts_http");
    // audio_pipeline_register(tts->pipeline, tts->mp3_decoder, "tts_mp3");
    // audio_pipeline_register(tts->pipeline, tts->i2s_writer, "tts_i2s");
    // const char *link_tag[3] = {"tts_http", "tts_mp3", "tts_i2s"};
    // audio_pipeline_link(tts->pipeline, &link_tag[0], 3);

    // // I2S流采样率 位数等设置
    // i2s_stream_set_clk(tts->i2s_writer, config->playback_sample_rate, 16, 1);
    return tts;
exit_tts_init:
    baidu_tts_destroy(tts);
    return NULL;
}

esp_err_t baidu_tts_destroy(baidu_tts_handle_t tts)
{
    if (tts == NULL)
    {
        return ESP_FAIL;
    }
    audio_pipeline_stop(tts->pipeline);
    audio_pipeline_wait_for_stop(tts->pipeline);
    audio_pipeline_terminate(tts->pipeline);
    audio_pipeline_remove_listener(tts->pipeline);
    audio_pipeline_deinit(tts->pipeline);
    free(tts->buffer);
    free(tts);
    return ESP_OK;
}

esp_err_t baidu_tts_set_listener(baidu_tts_handle_t tts, audio_event_iface_handle_t listener)
{
    if (listener)
    {
        audio_pipeline_set_listener(tts->pipeline, listener);
    }
    return ESP_OK;
}

bool baidu_tts_check_event_finish(baidu_tts_handle_t tts, audio_event_iface_msg_t *msg)
{
    if (msg->source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg->source == (void *)tts->i2s_writer && msg->cmd == AEL_MSG_CMD_REPORT_STATUS && (((int)msg->data == AEL_STATUS_STATE_STOPPED) || ((int)msg->data == AEL_STATUS_STATE_FINISHED)))
    {
        return true;
    }
    return false;
}

esp_err_t baidu_tts_start(baidu_tts_handle_t tts, const char *text)
{
    free(tts->text);
    tts->text = strdup(text);
    if (tts->text == NULL)
    {
        ESP_LOGE(TAG, "Error no mem");
        return ESP_ERR_NO_MEM;
    }

    // I2S流设置
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(0, 16000, 16, AUDIO_STREAM_WRITER);
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    tts->i2s_writer = i2s_stream_init(&i2s_cfg);
    // I2S流采样率 位数等设置
    i2s_stream_set_clk(tts->i2s_writer, tts->sample_rate, 16, 1);

    // http流设置
    http_stream_cfg_t http_cfg = {
        .type = AUDIO_STREAM_READER,
        .event_handle = _http_stream_reader_event_handle,
        .user_data = tts,
        .task_stack = BAIDU_TTS_TASK_STACK,
    };
    tts->http_stream_reader = http_stream_init(&http_cfg);

    // MP3流设置
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    tts->mp3_decoder = mp3_decoder_init(&mp3_cfg);

    audio_pipeline_register(tts->pipeline, tts->http_stream_reader, "tts_http");
    audio_pipeline_register(tts->pipeline, tts->mp3_decoder, "tts_mp3");
    audio_pipeline_register(tts->pipeline, tts->i2s_writer, "tts_i2s");
    const char *link_tag[3] = {"tts_http", "tts_mp3", "tts_i2s"};
    audio_pipeline_link(tts->pipeline, &link_tag[0], 3);

    snprintf(tts->buffer, tts->buffer_size, BAIDU_TTS_ENDPOINT);
    audio_pipeline_reset_items_state(tts->pipeline);
    audio_pipeline_reset_ringbuffer(tts->pipeline);
    audio_element_set_uri(tts->http_stream_reader, tts->buffer);
    audio_pipeline_run(tts->pipeline);
    return ESP_OK;
}

esp_err_t baidu_tts_stop(baidu_tts_handle_t tts)
{
    audio_pipeline_stop(tts->pipeline);
    audio_pipeline_wait_for_stop(tts->pipeline);
    ESP_LOGD(TAG, "TTS Stopped");

    // 销毁http流
    if (tts->http_stream_reader) {
        audio_pipeline_unregister(tts->pipeline, tts->http_stream_reader);
        audio_element_deinit(tts->http_stream_reader); // 销毁HTTP流
        tts->http_stream_reader = NULL;
    }
    // 销毁mp3流
    if (tts->mp3_decoder) {
        audio_pipeline_unregister(tts->pipeline, tts->mp3_decoder);
        audio_element_deinit(tts->mp3_decoder); // 销毁MP3流
        tts->mp3_decoder = NULL;
    }
        // 销毁流
    if (tts->i2s_writer) {
        audio_pipeline_unregister(tts->pipeline, tts->i2s_writer);
        audio_element_deinit(tts->i2s_writer); // 销毁流
        tts->i2s_writer = NULL;
    }

    return ESP_OK;
}
