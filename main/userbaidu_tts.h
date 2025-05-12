#pragma once
#include "esp_err.h"
#include "audio_event_iface.h"

#ifndef _BAIDU_TTS_H_
#define _BAIDU_TTS_H_

#include "esp_err.h"
#include "audio_event_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_TTS_BUFFER_SIZE (2048)

typedef struct baidu_tts* baidu_tts_handle_t;

typedef struct {
    const char *api_key;
    //const char *lang_code;
    int playback_sample_rate;
    int buffer_size;
} baidu_tts_config_t;

/**
 * @brief      Initialize Baidu Cloud Text-to-Speech, this function will return a Text-to-Speech context
 *
 * @param      config  The configuration
 *
 * @return     The Text-to-Speech context
 */
baidu_tts_handle_t baidu_tts_init(baidu_tts_config_t *config);

/**
 * @brief      Start sending text to Baidu Cloud Text-to-Speech and play audio received
 *
 * @param[in]  tts        The Text-to-Speech context
 * @param[in]  text       The text
 * @param[in]  lang_code  The language code
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t baidu_tts_start(baidu_tts_handle_t tts, const char *text);

/**
 * @brief      Stop playing audio from Baidu Cloud Text-to-Speech
 *
 * @param[in]  tts   The Text-to-Speech context
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t baidu_tts_stop(baidu_tts_handle_t tts);

/**
 * @brief      Register listener for the Text-to-Speech context
 *
 * @param[in]  tts       The Text-to-Speech context
 * @param[in]  listener  The listener
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t baidu_tts_set_listener(baidu_tts_handle_t tts, audio_event_iface_handle_t listener);

/**
 * @brief      Cleanup the Text-to-Speech object
 *
 * @param[in]  tts   The Text-to-Speech context
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t baidu_tts_destroy(baidu_tts_handle_t tts);

/**
 * @brief      Check if the Text-To-Speech finished playing audio from server
 *
 * @param[in]  tts   The Text-to-Speech context
 * @param      msg   The message
 *
 * @return
 *  - true
 *  - false
 */
bool baidu_tts_check_event_finish(baidu_tts_handle_t tts, audio_event_iface_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif




