/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_AUDIO_PCM_TRANSPORT_USB = 0,
    ESP_AUDIO_PCM_TRANSPORT_UART,
    ESP_AUDIO_PCM_TRANSPORT_TCP,
    ESP_AUDIO_PCM_TRANSPORT_UDP,
} esp_audio_pcm_transport_t;

typedef struct {
    int rx_buffer_size;
    int tx_buffer_size;
} esp_audio_pcm_usb_config_t;

typedef struct {
    uart_port_t port;
    int tx_pin;
    int rx_pin;
    int baud_rate;
    int rx_buffer_size;
    int tx_buffer_size;
} esp_audio_pcm_uart_config_t;

typedef struct {
    char server_ip[16];
    uint16_t port;
    int connect_timeout_ms;
    /** TX ring size in bytes; 0 = Kconfig default (1 MiB when PSRAM available). */
    int tx_buffer_size;
} esp_audio_pcm_tcp_config_t;

typedef struct {
    char server_ip[16];
    uint16_t pcm_port;
    uint16_t local_port;
    int connect_timeout_ms;
} esp_audio_pcm_udp_config_t;

typedef struct {
    esp_audio_pcm_transport_t type;
    union {
        esp_audio_pcm_usb_config_t usb;
        esp_audio_pcm_uart_config_t uart;
        esp_audio_pcm_tcp_config_t tcp;
        esp_audio_pcm_udp_config_t udp;
    };
} esp_audio_pcm_config_t;

typedef struct esp_audio_pcm *esp_audio_pcm_handle_t;

/**
 * Control command callbacks. Text format matches PC monitor / USB cmd:
 *   vol <0-100>
 *   gain <dB>
 *   gch <ch> <dB>
 *   stream <0|1>   (optional)
 */
typedef struct {
    void (*on_vol)(void *ctx, int vol);
    void (*on_gain_all)(void *ctx, float db);
    void (*on_gain_channel)(void *ctx, int ch, float db);
    void (*on_stream)(void *ctx, bool enabled);
    void *ctx;
} esp_audio_pcm_ctrl_cbs_t;

/** Create instance and install the underlying driver (USB/UART). */
esp_err_t esp_audio_pcm_new(const esp_audio_pcm_config_t *config, esp_audio_pcm_handle_t *out_io);

/** Stop control task, close, uninstall driver, and free instance. */
esp_err_t esp_audio_pcm_delete(esp_audio_pcm_handle_t io);

esp_err_t esp_audio_pcm_open(esp_audio_pcm_handle_t io);
esp_err_t esp_audio_pcm_close(esp_audio_pcm_handle_t io);

/** Read raw bytes (control channel uses the same transport RX path). */
int esp_audio_pcm_read(esp_audio_pcm_handle_t io, void *buf, size_t len, uint32_t timeout_ms);

/** Write raw bytes with retry; no-op when PCM stream is disabled. */
esp_err_t esp_audio_pcm_write(esp_audio_pcm_handle_t io, const void *data, size_t len, uint32_t timeout_ms);

void esp_audio_pcm_set_stream_enabled(esp_audio_pcm_handle_t io, bool enabled);
bool esp_audio_pcm_get_stream_enabled(const esp_audio_pcm_handle_t io);

esp_audio_pcm_transport_t esp_audio_pcm_get_type(const esp_audio_pcm_handle_t io);

/**
 * Background task: read text lines and dispatch control callbacks.
 * Does not echo responses on the transport (keeps PCM path clean).
 */
esp_err_t esp_audio_pcm_ctrl_start(esp_audio_pcm_handle_t io, const esp_audio_pcm_ctrl_cbs_t *cbs);
esp_err_t esp_audio_pcm_ctrl_stop(esp_audio_pcm_handle_t io);

/** Default USB Serial/JTAG config for PCM streaming. */
esp_audio_pcm_config_t esp_audio_pcm_config_default_usb(void);

/** Default UART config (Kconfig overrides pins/baud). */
esp_audio_pcm_config_t esp_audio_pcm_config_default_uart(void);

/** Default TCP client config (connects to PC monitor server). */
esp_audio_pcm_config_t esp_audio_pcm_config_default_tcp(void);

/** Default UDP client config (PCM to server, control on same socket). */
esp_audio_pcm_config_t esp_audio_pcm_config_default_udp(void);

/** Pick transport config from menuconfig (ESP_AUDIO_PCM_TRANSPORT_*). */
esp_audio_pcm_config_t esp_audio_pcm_config_default(void);

#ifdef __cplusplus
}
#endif
