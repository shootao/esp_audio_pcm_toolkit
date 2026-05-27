/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "esp_audio_pcm.h"
#include "esp_audio_pcm_priv.h"

#ifndef CONFIG_ESP_AUDIO_PCM_NET_SERVER_IP
#define CONFIG_ESP_AUDIO_PCM_NET_SERVER_IP "192.168.4.2"
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_TCP_PORT
#define CONFIG_ESP_AUDIO_PCM_TCP_PORT 8766
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_UDP_PCM_PORT
#define CONFIG_ESP_AUDIO_PCM_UDP_PCM_PORT 8766
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_UDP_LOCAL_PORT
#define CONFIG_ESP_AUDIO_PCM_UDP_LOCAL_PORT 0
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_NET_CONNECT_TIMEOUT_MS
#define CONFIG_ESP_AUDIO_PCM_NET_CONNECT_TIMEOUT_MS 5000
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_USB_RX_BUF_SIZE
#define CONFIG_ESP_AUDIO_PCM_USB_RX_BUF_SIZE 256
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_USB_TX_BUF_SIZE
#define CONFIG_ESP_AUDIO_PCM_USB_TX_BUF_SIZE 4096
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_UART_PORT
#define CONFIG_ESP_AUDIO_PCM_UART_PORT 1
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_UART_TX_PIN
#define CONFIG_ESP_AUDIO_PCM_UART_TX_PIN 17
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_UART_RX_PIN
#define CONFIG_ESP_AUDIO_PCM_UART_RX_PIN 18
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_UART_BAUD
#define CONFIG_ESP_AUDIO_PCM_UART_BAUD 921600
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_UART_RX_BUF_SIZE
#define CONFIG_ESP_AUDIO_PCM_UART_RX_BUF_SIZE 256
#endif
#ifndef CONFIG_ESP_AUDIO_PCM_UART_TX_BUF_SIZE
#define CONFIG_ESP_AUDIO_PCM_UART_TX_BUF_SIZE 4096
#endif

static const char *TAG = "esp_audio_pcm";

#define ESP_AUDIO_PCM_WRITE_RETRY_MAX 8

static const esp_audio_pcm_ops_t *esp_audio_pcm_lookup_ops(esp_audio_pcm_transport_t type)
{
    switch (type) {
    case ESP_AUDIO_PCM_TRANSPORT_USB:
        return esp_audio_pcm_ops_usb();
    case ESP_AUDIO_PCM_TRANSPORT_UART:
        return esp_audio_pcm_ops_uart();
    case ESP_AUDIO_PCM_TRANSPORT_TCP:
        return esp_audio_pcm_ops_tcp();
    case ESP_AUDIO_PCM_TRANSPORT_UDP:
        return esp_audio_pcm_ops_udp();
    default:
        return NULL;
    }
}

esp_audio_pcm_config_t esp_audio_pcm_config_default_usb(void)
{
    return (esp_audio_pcm_config_t) {
        .type = ESP_AUDIO_PCM_TRANSPORT_USB,
        .usb = {
            .rx_buffer_size = CONFIG_ESP_AUDIO_PCM_USB_RX_BUF_SIZE,
            .tx_buffer_size = CONFIG_ESP_AUDIO_PCM_USB_TX_BUF_SIZE,
        },
    };
}

esp_audio_pcm_config_t esp_audio_pcm_config_default_uart(void)
{
    return (esp_audio_pcm_config_t) {
        .type = ESP_AUDIO_PCM_TRANSPORT_UART,
        .uart = {
            .port = CONFIG_ESP_AUDIO_PCM_UART_PORT,
            .tx_pin = CONFIG_ESP_AUDIO_PCM_UART_TX_PIN,
            .rx_pin = CONFIG_ESP_AUDIO_PCM_UART_RX_PIN,
            .baud_rate = CONFIG_ESP_AUDIO_PCM_UART_BAUD,
            .rx_buffer_size = CONFIG_ESP_AUDIO_PCM_UART_RX_BUF_SIZE,
            .tx_buffer_size = CONFIG_ESP_AUDIO_PCM_UART_TX_BUF_SIZE,
        },
    };
}

esp_audio_pcm_config_t esp_audio_pcm_config_default_tcp(void)
{
    esp_audio_pcm_config_t cfg = {
        .type = ESP_AUDIO_PCM_TRANSPORT_TCP,
        .tcp = {
            .port = CONFIG_ESP_AUDIO_PCM_TCP_PORT,
            .connect_timeout_ms = CONFIG_ESP_AUDIO_PCM_NET_CONNECT_TIMEOUT_MS,
        },
    };
    strlcpy(cfg.tcp.server_ip, CONFIG_ESP_AUDIO_PCM_NET_SERVER_IP, sizeof(cfg.tcp.server_ip));
    return cfg;
}

esp_audio_pcm_config_t esp_audio_pcm_config_default_udp(void)
{
    esp_audio_pcm_config_t cfg = {
        .type = ESP_AUDIO_PCM_TRANSPORT_UDP,
        .udp = {
            .pcm_port = CONFIG_ESP_AUDIO_PCM_UDP_PCM_PORT,
            .local_port = CONFIG_ESP_AUDIO_PCM_UDP_LOCAL_PORT,
            .connect_timeout_ms = CONFIG_ESP_AUDIO_PCM_NET_CONNECT_TIMEOUT_MS,
        },
    };
    strlcpy(cfg.udp.server_ip, CONFIG_ESP_AUDIO_PCM_NET_SERVER_IP, sizeof(cfg.udp.server_ip));
    return cfg;
}

esp_audio_pcm_config_t esp_audio_pcm_config_default(void)
{
#if CONFIG_ESP_AUDIO_PCM_TRANSPORT_UART
    return esp_audio_pcm_config_default_uart();
#elif CONFIG_ESP_AUDIO_PCM_TRANSPORT_TCP
    return esp_audio_pcm_config_default_tcp();
#elif CONFIG_ESP_AUDIO_PCM_TRANSPORT_UDP
    return esp_audio_pcm_config_default_udp();
#else
    return esp_audio_pcm_config_default_usb();
#endif
}

esp_err_t esp_audio_pcm_new(const esp_audio_pcm_config_t *config, esp_audio_pcm_handle_t *out_io)
{
    if (config == NULL || out_io == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_audio_pcm_ops_t *ops = esp_audio_pcm_lookup_ops(config->type);
    if (ops == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    struct esp_audio_pcm *io = calloc(1, sizeof(struct esp_audio_pcm));
    if (io == NULL) {
        return ESP_ERR_NO_MEM;
    }

    io->config = *config;
    io->ops = ops;
    io->stream_enabled = true;

    if (ops->init == NULL) {
        free(io);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ops->init(io);
    if (ret != ESP_OK) {
        free(io);
        return ret;
    }

    io->initialized = true;
    *out_io = io;
    return ESP_OK;
}

esp_err_t esp_audio_pcm_open(esp_audio_pcm_handle_t io)
{
    if (io == NULL || io->ops == NULL || io->ops->open == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!io->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (io->opened) {
        return ESP_OK;
    }

    esp_err_t ret = io->ops->open(io);
    if (ret == ESP_OK) {
        io->opened = true;
    }
    return ret;
}

esp_err_t esp_audio_pcm_close(esp_audio_pcm_handle_t io)
{
    if (io == NULL || io->ops == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!io->opened) {
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    if (io->ops->close) {
        ret = io->ops->close(io);
    }
    io->opened = false;
    return ret;
}

static esp_err_t esp_audio_pcm_deinit(esp_audio_pcm_handle_t io)
{
    if (io == NULL || io->ops == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!io->initialized) {
        return ESP_OK;
    }
    if (io->opened) {
        esp_audio_pcm_close(io);
    }

    esp_err_t ret = ESP_OK;
    if (io->ops->deinit) {
        ret = io->ops->deinit(io);
    }
    io->initialized = false;
    return ret;
}

esp_err_t esp_audio_pcm_delete(esp_audio_pcm_handle_t io)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_audio_pcm_ctrl_stop(io);
    if (io->opened) {
        esp_audio_pcm_close(io);
    }
    if (io->initialized) {
        esp_audio_pcm_deinit(io);
    }
    free(io);
    return ESP_OK;
}

int esp_audio_pcm_read(esp_audio_pcm_handle_t io, void *buf, size_t len, uint32_t timeout_ms)
{
    if (io == NULL || io->ops == NULL || io->ops->read == NULL || buf == NULL || len == 0) {
        return -1;
    }
    if (!io->opened) {
        return -1;
    }
    return io->ops->read(io, buf, len, timeout_ms);
}

esp_err_t esp_audio_pcm_write(esp_audio_pcm_handle_t io, const void *data, size_t len, uint32_t timeout_ms)
{
    if (io == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!io->opened || !io->stream_enabled) {
        return ESP_OK;
    }
    if (io->ops == NULL || io->ops->write == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int retry = 0; retry < ESP_AUDIO_PCM_WRITE_RETRY_MAX; retry++) {
        int sent = io->ops->write(io, data, len, timeout_ms);
        if (sent == (int)len) {
            return ESP_OK;
        }
        if (sent >= 0) {
            continue;
        }
#if defined(errno) && (errno != 0)
        if (errno != ENOMEM && errno != EAGAIN) {
            return ESP_FAIL;
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(1 + retry));
    }

    return ESP_ERR_NO_MEM;
}

void esp_audio_pcm_set_stream_enabled(esp_audio_pcm_handle_t io, bool enabled)
{
    if (io != NULL) {
        io->stream_enabled = enabled;
    }
}

bool esp_audio_pcm_get_stream_enabled(const esp_audio_pcm_handle_t io)
{
    return io != NULL && io->stream_enabled;
}

esp_audio_pcm_transport_t esp_audio_pcm_get_type(const esp_audio_pcm_handle_t io)
{
    if (io == NULL) {
        return ESP_AUDIO_PCM_TRANSPORT_USB;
    }
    return io->config.type;
}
