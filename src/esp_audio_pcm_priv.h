/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_audio_pcm.h"

typedef struct esp_audio_pcm_ops esp_audio_pcm_ops_t;

struct esp_audio_pcm_ops {
    esp_err_t (*init)(esp_audio_pcm_handle_t io);
    esp_err_t (*deinit)(esp_audio_pcm_handle_t io);
    esp_err_t (*open)(esp_audio_pcm_handle_t io);
    esp_err_t (*close)(esp_audio_pcm_handle_t io);
    int (*read)(esp_audio_pcm_handle_t io, void *buf, size_t len, uint32_t timeout_ms);
    int (*write)(esp_audio_pcm_handle_t io, const void *data, size_t len, uint32_t timeout_ms);
};

struct esp_audio_pcm {
    esp_audio_pcm_config_t config;
    const esp_audio_pcm_ops_t *ops;
    void *transport_ctx;
    bool initialized;
    bool opened;
    bool stream_enabled;
    bool ctrl_running;
    esp_audio_pcm_ctrl_cbs_t ctrl_cbs;
    TaskHandle_t ctrl_task;
};

const esp_audio_pcm_ops_t *esp_audio_pcm_ops_usb(void);
const esp_audio_pcm_ops_t *esp_audio_pcm_ops_uart(void);
const esp_audio_pcm_ops_t *esp_audio_pcm_ops_tcp(void);
const esp_audio_pcm_ops_t *esp_audio_pcm_ops_udp(void);
