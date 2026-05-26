/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_log.h"

#include "esp_audio_pcm_priv.h"

static const char *TAG = "esp_audio_pcm_usb";

static esp_err_t usb_init(esp_audio_pcm_handle_t io)
{
    const esp_audio_pcm_usb_config_t *cfg = &io->config.usb;
    usb_serial_jtag_driver_config_t usb_cfg = {
        .rx_buffer_size = cfg->rx_buffer_size > 0 ? cfg->rx_buffer_size : 256,
        .tx_buffer_size = cfg->tx_buffer_size > 0 ? cfg->tx_buffer_size : 4096,
    };
    esp_err_t ret = usb_serial_jtag_driver_install(&usb_cfg);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "USB Serial/JTAG driver already installed");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "usb_serial_jtag_driver_install failed");
    ESP_LOGI(TAG, "USB Serial/JTAG ready (rx=%d tx=%d)", usb_cfg.rx_buffer_size, usb_cfg.tx_buffer_size);
    return ESP_OK;
}

static esp_err_t usb_deinit(esp_audio_pcm_handle_t io)
{
    (void)io;
    return usb_serial_jtag_driver_uninstall();
}

static esp_err_t usb_open(esp_audio_pcm_handle_t io)
{
    (void)io;
    return ESP_OK;
}

static esp_err_t usb_close(esp_audio_pcm_handle_t io)
{
    (void)io;
    return ESP_OK;
}

static int usb_read(esp_audio_pcm_handle_t io, void *buf, size_t len, uint32_t timeout_ms)
{
    (void)io;
    return usb_serial_jtag_read_bytes(buf, len, pdMS_TO_TICKS(timeout_ms));
}

static int usb_write(esp_audio_pcm_handle_t io, const void *data, size_t len, uint32_t timeout_ms)
{
    (void)io;
    return usb_serial_jtag_write_bytes((const char *)data, len, pdMS_TO_TICKS(timeout_ms));
}

static const esp_audio_pcm_ops_t s_usb_ops = {
    .init = usb_init,
    .deinit = usb_deinit,
    .open = usb_open,
    .close = usb_close,
    .read = usb_read,
    .write = usb_write,
};

const esp_audio_pcm_ops_t *esp_audio_pcm_ops_usb(void)
{
    return &s_usb_ops;
}
