/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

/** Connect WiFi STA when PCM transport is TCP/UDP; no-op for USB/UART. */
esp_err_t app_net_init(void);
