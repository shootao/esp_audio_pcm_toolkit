/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include "board.h"
#include "driver/i2s_std.h"
#include "esp_audio_pcm.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define TAG                     "basic_example"

#define SAMPLE_RATE_HZ          16000
#define PLAY_BITS               16
#define PLAY_CHANNELS           1
#define RECORD_BITS             16
#define MIC_GAIN_CH_MAX         4
#define MIC_GAIN_DB_MAX         36.5f
#define RECORD_CHUNK_SAMPLES    256
#define RECORD_CHUNK_BYTES      (RECORD_CHUNK_SAMPLES * CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS * (RECORD_BITS / 8))

typedef struct {
    esp_audio_pcm_handle_t pcm;
    esp_codec_dev_handle_t play_dev;
    esp_codec_dev_handle_t record_dev;
    int play_vol;
    float mic_gain_db[MIC_GAIN_CH_MAX];
    bool use_codec;
} app_state_t;

static app_state_t s_app;

static esp_codec_dev_sample_info_t play_sample_info(void)
{
    return (esp_codec_dev_sample_info_t) {
        .sample_rate = SAMPLE_RATE_HZ,
        .channel = PLAY_CHANNELS,
        .bits_per_sample = PLAY_BITS,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };
}

static esp_codec_dev_sample_info_t record_sample_info(void)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = SAMPLE_RATE_HZ,
        .channel = 4,
        .bits_per_sample = RECORD_BITS,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };

#if CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS == 3
    fs.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)
                      | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)
                      | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2);
#else
    fs.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0)
                      | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)
                      | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2)
                      | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3);
#endif
    return fs;
}

static float clamp_mic_gain_db(float db)
{
    if (db < 0.0f) {
        return 0.0f;
    }
    if (db > MIC_GAIN_DB_MAX) {
        return MIC_GAIN_DB_MAX;
    }
    return db;
}

static void init_mic_gain_defaults(void)
{
    for (int i = 0; i < MIC_GAIN_CH_MAX; i++) {
        s_app.mic_gain_db[i] = (float)CONFIG_BASIC_EXAMPLE_DEFAULT_MIC_GAIN;
    }
}

static int active_mic_channel_count(void)
{
#if CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS == 3
    return 3;
#else
    return 4;
#endif
}

static void apply_mic_gain(esp_codec_dev_handle_t record_dev)
{
    if (record_dev == NULL) {
        return;
    }

    float max_gain = 0.0f;
    const int mic_count = active_mic_channel_count();

    for (int i = 0; i < mic_count; i++) {
        esp_codec_dev_set_in_channel_gain(record_dev,
                                          ESP_CODEC_DEV_MAKE_CHANNEL_MASK(i),
                                          s_app.mic_gain_db[i]);
        if (s_app.mic_gain_db[i] > max_gain) {
            max_gain = s_app.mic_gain_db[i];
        }
    }
    esp_codec_dev_set_in_gain(record_dev, max_gain);
}

static void set_playback_volume(int vol)
{
    if (vol < 0) {
        vol = 0;
    }
    if (vol > 100) {
        vol = 100;
    }
    s_app.play_vol = vol;

    if (s_app.play_dev != NULL) {
        esp_codec_dev_set_out_vol(s_app.play_dev, s_app.play_vol);
    }
    ESP_LOGI(TAG, "Playback volume -> %d", s_app.play_vol);
}

static void set_mic_gain_channel(int ch, float db)
{
    if (ch < 0 || ch >= MIC_GAIN_CH_MAX) {
        return;
    }
    s_app.mic_gain_db[ch] = clamp_mic_gain_db(db);

    if (s_app.record_dev != NULL) {
        esp_codec_dev_set_in_channel_gain(s_app.record_dev,
                                          ESP_CODEC_DEV_MAKE_CHANNEL_MASK(ch),
                                          s_app.mic_gain_db[ch]);
    }
    ESP_LOGI(TAG, "Mic Ch%d gain -> %.1f dB", ch, (double)s_app.mic_gain_db[ch]);
}

static void set_mic_gain_all(float db)
{
    db = clamp_mic_gain_db(db);
    for (int i = 0; i < MIC_GAIN_CH_MAX; i++) {
        s_app.mic_gain_db[i] = db;
    }
    apply_mic_gain(s_app.record_dev);
    ESP_LOGI(TAG, "Mic gain (all) -> %.1f dB", (double)db);
}

static void on_vol(void *ctx, int vol)
{
    (void)ctx;
    set_playback_volume(vol);
}

static void on_gain_all(void *ctx, float db)
{
    (void)ctx;
    set_mic_gain_all(db);
}

static void on_gain_channel(void *ctx, int ch, float db)
{
    (void)ctx;
    set_mic_gain_channel(ch, db);
}

static void on_stream(void *ctx, bool enabled)
{
    app_state_t *app = (app_state_t *)ctx;
    esp_audio_pcm_set_stream_enabled(app->pcm, enabled);
    ESP_LOGI(TAG, "PCM stream -> %s", enabled ? "on" : "off");
}

static void fill_dummy_pcm(int16_t *samples, int frame_count, uint32_t *phase)
{
    const int channels = CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS;

    for (int i = 0; i < frame_count; i++) {
        for (int ch = 0; ch < channels; ch++) {
            uint32_t p = phase[ch]++;
            int32_t amp = 4000 + ch * 1500;
            samples[i * channels + ch] = (int16_t)((p & 0xFF) * amp / 128 - amp / 2);
        }
    }
}

static esp_err_t open_playback_codec(void)
{
    if (s_app.play_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_codec_dev_sample_info_t fs = play_sample_info();
    if (esp_codec_dev_open(s_app.play_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open(play) failed");
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(s_app.play_dev, s_app.play_vol);
    return ESP_OK;
}

static void record_task(void *arg)
{
    app_state_t *app = (app_state_t *)arg;
    uint8_t buffer[RECORD_CHUNK_BYTES];
    uint32_t phase[MIC_GAIN_CH_MAX] = {0};
    uint32_t chunk_count = 0;

    if (app->use_codec && app->record_dev != NULL) {
        esp_codec_dev_sample_info_t fs = record_sample_info();
        if (esp_codec_dev_open(app->record_dev, &fs) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "esp_codec_dev_open(record) failed, fallback to dummy PCM");
            app->use_codec = false;
        } else {
            apply_mic_gain(app->record_dev);
            ESP_LOGI(TAG, "Recording %d ch @ %d Hz via esp_codec_dev_read",
                     CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS, SAMPLE_RATE_HZ);
        }
    }

    if (!app->use_codec) {
        ESP_LOGW(TAG, "Dummy PCM: %d Hz / %d ch (no codec)", SAMPLE_RATE_HZ,
                 CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS);
    }

    ESP_LOGI(TAG, "PC monitor: Channels=%d, Rate=%d",
             CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS, SAMPLE_RATE_HZ);

    while (true) {
        if (app->use_codec && app->record_dev != NULL) {
            if (esp_codec_dev_read(app->record_dev, buffer, sizeof(buffer)) != ESP_CODEC_DEV_OK) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        } else {
            fill_dummy_pcm((int16_t *)buffer, RECORD_CHUNK_SAMPLES, phase);
            vTaskDelay(pdMS_TO_TICKS(16));
        }

        if (app->pcm != NULL && esp_audio_pcm_get_stream_enabled(app->pcm)) {
            esp_audio_pcm_write(app->pcm, buffer, sizeof(buffer), 20);
        }

        if (++chunk_count % 64 == 0) {
            ESP_LOGI(TAG, "Streaming vol=%d ch0_gain=%.1f dB codec=%d",
                     app->play_vol, (double)app->mic_gain_db[0], app->use_codec);
        }
    }
}

void app_main(void)
{
    memset(&s_app, 0, sizeof(s_app));
    s_app.play_vol = CONFIG_BASIC_EXAMPLE_DEFAULT_PLAY_VOL;
    init_mic_gain_defaults();

    audio_board_init();
    s_app.play_dev = audio_board_get_play_dev_handle();
    s_app.record_dev = audio_board_get_record_dev_handle();
    s_app.use_codec = (s_app.play_dev != NULL && s_app.record_dev != NULL);

    if (s_app.use_codec) {
        if (open_playback_codec() != ESP_OK) {
            ESP_LOGW(TAG, "Playback codec open failed");
        }
    } else {
        ESP_LOGW(TAG, "Board codec unavailable, using dummy PCM only");
    }

    esp_audio_pcm_config_t cfg = esp_audio_pcm_config_default();
    ESP_ERROR_CHECK(esp_audio_pcm_new(&cfg, &s_app.pcm));
    ESP_ERROR_CHECK(esp_audio_pcm_open(s_app.pcm));
    esp_audio_pcm_set_stream_enabled(s_app.pcm, true);

    const esp_audio_pcm_ctrl_cbs_t cbs = {
        .on_vol = on_vol,
        .on_gain_all = on_gain_all,
        .on_gain_channel = on_gain_channel,
        .on_stream = on_stream,
        .ctx = &s_app,
    };
    ESP_ERROR_CHECK(esp_audio_pcm_ctrl_start(s_app.pcm, &cbs));

    ESP_LOGI(TAG, "Transport: %s | Logs: UART0",
             esp_audio_pcm_get_type(s_app.pcm) == ESP_AUDIO_PCM_TRANSPORT_UART ? "UART" : "USB");
    ESP_LOGI(TAG, "Remote cmd: vol / gain / gch / stream");

    xTaskCreate(record_task, "record_task", 8192, &s_app, 5, NULL);
}
