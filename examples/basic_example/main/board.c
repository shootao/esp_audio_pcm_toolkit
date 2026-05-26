/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "board.h"

#include "bsp/hi_nomi_board.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "board";

static esp_codec_dev_handle_t s_play_dev;
static esp_codec_dev_handle_t s_record_dev;

#define BOARD_SAMPLE_RATE   16000

void audio_board_init(void)
{
    bsp_audio_init_cfg_t audio_cfg = {
        .sample_rate_hz = BOARD_SAMPLE_RATE,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };

    ESP_LOGI(TAG, "Init Hi Nomi BSP (ES8311 + ES7210) @ %d Hz", BOARD_SAMPLE_RATE);
    ESP_ERROR_CHECK(bsp_audio_init(&audio_cfg));

    s_play_dev = bsp_audio_codec_speaker_init();
    s_record_dev = bsp_audio_codec_microphone_init();

    if (s_play_dev == NULL) {
        ESP_LOGE(TAG, "ES8311 speaker init failed");
    }
    if (s_record_dev == NULL) {
        ESP_LOGE(TAG, "ES7210 microphone init failed");
    }
}

esp_codec_dev_handle_t audio_board_get_play_dev_handle(void)
{
    return s_play_dev;
}

esp_codec_dev_handle_t audio_board_get_record_dev_handle(void)
{
    return s_record_dev;
}
