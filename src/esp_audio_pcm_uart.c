/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"

#include "esp_audio_pcm_priv.h"

static const char *TAG = "esp_audio_pcm_uart";

static esp_err_t uart_transport_init(esp_audio_pcm_handle_t io)
{
    const esp_audio_pcm_uart_config_t *cfg = &io->config.uart;

    uart_config_t uart_cfg = {
        .baud_rate = cfg->baud_rate > 0 ? cfg->baud_rate : 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_param_config(cfg->port, &uart_cfg), TAG, "uart_param_config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(cfg->port, cfg->tx_pin, cfg->rx_pin,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin failed");

    int rx_buf = cfg->rx_buffer_size > 0 ? cfg->rx_buffer_size : 256;
    int tx_buf = cfg->tx_buffer_size > 0 ? cfg->tx_buffer_size : 4096;
    esp_err_t ret = uart_driver_install(cfg->port, rx_buf, tx_buf, 0, NULL, 0);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "UART%d driver already installed", cfg->port);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "uart_driver_install failed");
    ESP_LOGI(TAG, "UART%d ready (tx=%d rx=%d baud=%d)", cfg->port, cfg->tx_pin, cfg->rx_pin, uart_cfg.baud_rate);
    return ESP_OK;
}

static esp_err_t uart_transport_deinit(esp_audio_pcm_handle_t io)
{
    return uart_driver_delete(io->config.uart.port);
}

static esp_err_t uart_transport_open(esp_audio_pcm_handle_t io)
{
    (void)io;
    return ESP_OK;
}

static esp_err_t uart_transport_close(esp_audio_pcm_handle_t io)
{
    (void)io;
    return ESP_OK;
}

static int uart_transport_read(esp_audio_pcm_handle_t io, void *buf, size_t len, uint32_t timeout_ms)
{
    int n = uart_read_bytes(io->config.uart.port, buf, len, pdMS_TO_TICKS(timeout_ms));
    return n;
}

static int uart_transport_write(esp_audio_pcm_handle_t io, const void *data, size_t len, uint32_t timeout_ms)
{
    int n = uart_write_bytes(io->config.uart.port, data, len);
    if (n < 0) {
        return n;
    }
    if (timeout_ms > 0) {
        uart_wait_tx_done(io->config.uart.port, pdMS_TO_TICKS(timeout_ms));
    }
    return n;
}

static const esp_audio_pcm_ops_t s_uart_ops = {
    .init = uart_transport_init,
    .deinit = uart_transport_deinit,
    .open = uart_transport_open,
    .close = uart_transport_close,
    .read = uart_transport_read,
    .write = uart_transport_write,
};

const esp_audio_pcm_ops_t *esp_audio_pcm_ops_uart(void)
{
    return &s_uart_ops;
}
