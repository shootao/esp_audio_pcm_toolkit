/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_audio_pcm.h"
#include "esp_audio_pcm_priv.h"

#define ESP_AUDIO_PCM_LINE_MAX 64

static void esp_audio_pcm_dispatch_line(esp_audio_pcm_handle_t io, char *line)
{
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0') {
        return;
    }

    int vol = 0;
    if (sscanf(line, "vol %d", &vol) == 1) {
        if (io->ctrl_cbs.on_vol) {
            io->ctrl_cbs.on_vol(io->ctrl_cbs.ctx, vol);
        }
        return;
    }

    int ch = 0;
    float db = 0.0f;
    if (sscanf(line, "gch %d %f", &ch, &db) == 2) {
        if (io->ctrl_cbs.on_gain_channel) {
            io->ctrl_cbs.on_gain_channel(io->ctrl_cbs.ctx, ch, db);
        }
        return;
    }

    if (sscanf(line, "gain %f", &db) == 1) {
        if (io->ctrl_cbs.on_gain_all) {
            io->ctrl_cbs.on_gain_all(io->ctrl_cbs.ctx, db);
        }
        return;
    }

    int stream_en = 0;
    if (sscanf(line, "stream %d", &stream_en) == 1) {
        if (io->ctrl_cbs.on_stream) {
            io->ctrl_cbs.on_stream(io->ctrl_cbs.ctx, stream_en != 0);
        }
    }
}

static void esp_audio_pcm_ctrl_task(void *arg)
{
    esp_audio_pcm_handle_t io = arg;
    char line[ESP_AUDIO_PCM_LINE_MAX];
    int pos = 0;
    uint8_t rx[32];

    while (io->ctrl_running) {
        int n = esp_audio_pcm_read(io, rx, sizeof(rx), 50);
        if (n <= 0) {
            continue;
        }

        for (int i = 0; i < n; i++) {
            char c = (char)rx[i];
            if (c == '\r' || c == '\n') {
                line[pos] = '\0';
                esp_audio_pcm_dispatch_line(io, line);
                pos = 0;
                continue;
            }
            if (pos < ESP_AUDIO_PCM_LINE_MAX - 1) {
                line[pos++] = c;
            }
        }
    }

    io->ctrl_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t esp_audio_pcm_ctrl_start(esp_audio_pcm_handle_t io, const esp_audio_pcm_ctrl_cbs_t *cbs)
{
    if (io == NULL || cbs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (io->ctrl_running) {
        return ESP_ERR_INVALID_STATE;
    }

    io->ctrl_cbs = *cbs;
    io->ctrl_running = true;
    if (xTaskCreate(esp_audio_pcm_ctrl_task, "esp_audio_pcm_ctrl", 3072, io, 4, &io->ctrl_task) != pdPASS) {
        io->ctrl_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t esp_audio_pcm_ctrl_stop(esp_audio_pcm_handle_t io)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!io->ctrl_running) {
        return ESP_OK;
    }

    io->ctrl_running = false;
    while (io->ctrl_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    memset(&io->ctrl_cbs, 0, sizeof(io->ctrl_cbs));
    return ESP_OK;
}
