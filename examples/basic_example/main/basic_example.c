/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "esp_audio_pcm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_init.h"
#include "sdkconfig.h"

#define TAG                     "basic_example"

#define SAMPLE_RATE_HZ          16000
#define RECORD_BITS             16
#define MIC_GAIN_CH_MAX         4
#define MIC_GAIN_DB_MAX         36.5f
#define RECORD_CHUNK_SAMPLES    256
#define RECORD_CHUNK_BYTES      (RECORD_CHUNK_SAMPLES * CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS * (RECORD_BITS / 8))

#define SINE_AMP_BASE           12000
#define PLAY_SQ_FREQ_HZ         440
#define PLAY_SQ_AMP_MAX         16000

/* Per-mic sine frequencies (Hz) — distinct on PC monitor waveform */
static const int s_mic_freq_hz[MIC_GAIN_CH_MAX] = { 220, 440, 660, 880 };

typedef struct {
    esp_audio_pcm_handle_t pcm;
    int play_vol;
    float mic_gain_db[MIC_GAIN_CH_MAX];
    volatile bool play_running;
} app_state_t;

static app_state_t s_app;

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

static float db_to_linear(float db)
{
    return powf(10.0f, db / 20.0f);
}

static void init_mic_gain_defaults(void)
{
    for (int i = 0; i < MIC_GAIN_CH_MAX; i++) {
        s_app.mic_gain_db[i] = (float)CONFIG_BASIC_EXAMPLE_DEFAULT_MIC_GAIN;
    }
}

static const char *pcm_transport_name(esp_audio_pcm_transport_t type)
{
    switch (type) {
    case ESP_AUDIO_PCM_TRANSPORT_UART:
        return "UART";
    case ESP_AUDIO_PCM_TRANSPORT_TCP:
        return "TCP";
    case ESP_AUDIO_PCM_TRANSPORT_UDP:
        return "UDP";
    default:
        return "USB";
    }
}

#if CONFIG_ESP_AUDIO_PCM_TRANSPORT_TCP
static void log_pcm_network_target(void)
{
    ESP_LOGI(TAG, "PCM TCP -> %s:%d", CONFIG_ESP_AUDIO_PCM_NET_SERVER_IP, CONFIG_ESP_AUDIO_PCM_TCP_PORT);
}
#elif CONFIG_ESP_AUDIO_PCM_TRANSPORT_UDP
static void log_pcm_network_target(void)
{
    ESP_LOGI(TAG, "PCM UDP -> %s:%d (local port %d)",
             CONFIG_ESP_AUDIO_PCM_NET_SERVER_IP,
             CONFIG_ESP_AUDIO_PCM_UDP_PCM_PORT,
             CONFIG_ESP_AUDIO_PCM_UDP_LOCAL_PORT);
}
#else
static void log_pcm_network_target(void)
{
}
#endif

static void configure_pcm_logging(void)
{
#if CONFIG_ESP_AUDIO_PCM_TRANSPORT_USB
    /*
     * USB Serial/JTAG carries PCM. Console defaults to UART0 (sdkconfig.defaults),
     * so ESP_LOG on UART is OK. If Console is moved to USB, set ESP_LOG_NONE here.
     */
#elif CONFIG_ESP_AUDIO_PCM_TRANSPORT_UART
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
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
    ESP_LOGI(TAG, "Playback volume -> %d (square-wave sim, no DAC)", s_app.play_vol);
}

static void set_mic_gain_channel(int ch, float db)
{
    if (ch < 0 || ch >= MIC_GAIN_CH_MAX) {
        return;
    }
    s_app.mic_gain_db[ch] = clamp_mic_gain_db(db);
    ESP_LOGI(TAG, "Mic Ch%d gain -> %.1f dB (sine tone amplitude)", ch, (double)s_app.mic_gain_db[ch]);
}

static void set_mic_gain_all(float db)
{
    db = clamp_mic_gain_db(db);
    for (int i = 0; i < MIC_GAIN_CH_MAX; i++) {
        s_app.mic_gain_db[i] = db;
    }
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

static void fill_sine_pcm(const app_state_t *app, int16_t *samples, int frame_count, uint32_t *sample_ix)
{
    const int channels = CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS;

    for (int i = 0; i < frame_count; i++) {
        const float t = (float)(*sample_ix) / (float)SAMPLE_RATE_HZ;
        (*sample_ix)++;

        for (int ch = 0; ch < channels; ch++) {
            const float gain = db_to_linear(app->mic_gain_db[ch]);
            float amp = (float)SINE_AMP_BASE * gain;
            if (amp > 30000.0f) {
                amp = 30000.0f;
            }
            const float rad = 2.0f * (float)M_PI * (float)s_mic_freq_hz[ch] * t;
            samples[i * channels + ch] = (int16_t)(sinf(rad) * amp);
        }
    }
}

static void record_task(void *arg)
{
    app_state_t *app = (app_state_t *)arg;
    uint8_t buffer[RECORD_CHUNK_BYTES];
    uint32_t sample_ix = 0;

    ESP_LOGI(TAG, "Record: %d-ch sine @ %d,%d,%d,%d Hz (first %d ch)",
             CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS,
             s_mic_freq_hz[0], s_mic_freq_hz[1], s_mic_freq_hz[2], s_mic_freq_hz[3],
             CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS);

    while (true) {
        fill_sine_pcm(app, (int16_t *)buffer, RECORD_CHUNK_SAMPLES, &sample_ix);
        vTaskDelay(pdMS_TO_TICKS(16));

        if (app->pcm != NULL && esp_audio_pcm_get_stream_enabled(app->pcm)) {
            esp_audio_pcm_write(app->pcm, buffer, sizeof(buffer), 20);
        }
    }
}

/**
 * Software playback: square wave at PLAY_SQ_FREQ_HZ, amplitude from play_vol.
 * No I2S/DAC — replace the body with esp_codec_dev_write() when you add hardware.
 */
static void play_task(void *arg)
{
    app_state_t *app = (app_state_t *)arg;
    const int delay_ms = 1000 / (PLAY_SQ_FREQ_HZ * 2);
    bool high = true;

    ESP_LOGI(TAG, "Play sim: %d Hz square, vol=%d (no DAC)", PLAY_SQ_FREQ_HZ, app->play_vol);

    while (app->play_running) {
        const float vol = app->play_vol / 100.0f;
        const int16_t sample = high
                                   ? (int16_t)(PLAY_SQ_AMP_MAX * vol)
                                   : (int16_t)(-PLAY_SQ_AMP_MAX * vol);
        (void)sample;

        high = !high;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    memset(&s_app, 0, sizeof(s_app));
    s_app.play_vol = CONFIG_BASIC_EXAMPLE_DEFAULT_PLAY_VOL;
    s_app.play_running = true;
    init_mic_gain_defaults();

    configure_pcm_logging();

    esp_audio_pcm_config_t cfg = esp_audio_pcm_config_default();
    ESP_ERROR_CHECK(esp_audio_pcm_new(&cfg, &s_app.pcm));
    ESP_ERROR_CHECK(app_net_init());

    esp_err_t open_ret = esp_audio_pcm_open(s_app.pcm);
    if (open_ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_audio_pcm_open failed: %s (transport=%s)",
                 esp_err_to_name(open_ret), pcm_transport_name(cfg.type));
        ESP_LOGE(TAG, "UDP/TCP: check PC monitor server IP/port; run idf.py fullclean build");
    }
    ESP_ERROR_CHECK(open_ret);
    esp_audio_pcm_set_stream_enabled(s_app.pcm, true);

    const esp_audio_pcm_ctrl_cbs_t cbs = {
        .on_vol = on_vol,
        .on_gain_all = on_gain_all,
        .on_gain_channel = on_gain_channel,
        .on_stream = on_stream,
        .ctx = &s_app,
    };
    ESP_ERROR_CHECK(esp_audio_pcm_ctrl_start(s_app.pcm, &cbs));

    ESP_LOGI(TAG, "Transport: %s", pcm_transport_name(esp_audio_pcm_get_type(s_app.pcm)));
    log_pcm_network_target();
    ESP_LOGI(TAG, "Remote cmd: vol / gain / gch / stream");
#if CONFIG_ESP_AUDIO_PCM_TRANSPORT_USB || CONFIG_ESP_AUDIO_PCM_TRANSPORT_UART
    ESP_LOGI(TAG, "PC monitor: Channels=%d Rate=%d; UART baud=%d if hardware UART",
             CONFIG_BASIC_EXAMPLE_RECORD_CHANNELS, SAMPLE_RATE_HZ, CONFIG_ESP_AUDIO_PCM_UART_BAUD);
#endif

    xTaskCreate(play_task, "play_task", 3072, &s_app, 4, NULL);
    xTaskCreate(record_task, "record_task", 4096, &s_app, 5, NULL);
}
