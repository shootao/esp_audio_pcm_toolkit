/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_codec_dev.h"

void audio_board_init(void);

esp_codec_dev_handle_t audio_board_get_play_dev_handle(void);

esp_codec_dev_handle_t audio_board_get_record_dev_handle(void);
