/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "esp_audio_pcm_priv.h"

static const char *TAG = "esp_audio_pcm_tcp";

typedef struct {
    int sock;
} tcp_ctx_t;

static tcp_ctx_t *tcp_ctx(esp_audio_pcm_handle_t io)
{
    return (tcp_ctx_t *)io->transport_ctx;
}

static void tcp_close_socket(tcp_ctx_t *ctx)
{
    if (ctx == NULL || ctx->sock < 0) {
        return;
    }
    shutdown(ctx->sock, SHUT_RDWR);
    close(ctx->sock);
    ctx->sock = -1;
}

static esp_err_t tcp_connect(esp_audio_pcm_handle_t io)
{
    const esp_audio_pcm_tcp_config_t *cfg = &io->config.tcp;
    tcp_ctx_t *ctx = tcp_ctx(io);

    if (ctx->sock >= 0) {
        tcp_close_socket(ctx);
    }

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)cfg->port);

    int err = getaddrinfo(cfg->server_ip, port_str, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "getaddrinfo(%s:%s) failed: %d", cfg->server_ip, port_str, err);
        return ESP_FAIL;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        freeaddrinfo(res);
        return ESP_FAIL;
    }

    int timeout_ms = cfg->connect_timeout_ms > 0 ? cfg->connect_timeout_ms : 5000;
    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "connect(%s:%s) failed: errno %d", cfg->server_ip, port_str, errno);
        close(sock);
        freeaddrinfo(res);
        return ESP_FAIL;
    }
    freeaddrinfo(res);

    ctx->sock = sock;
    ESP_LOGI(TAG, "TCP connected to %s:%u", cfg->server_ip, (unsigned)cfg->port);
    return ESP_OK;
}

static esp_err_t tcp_init(esp_audio_pcm_handle_t io)
{
    tcp_ctx_t *ctx = calloc(1, sizeof(tcp_ctx_t));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->sock = -1;
    io->transport_ctx = ctx;
    return ESP_OK;
}

static esp_err_t tcp_deinit(esp_audio_pcm_handle_t io)
{
    tcp_ctx_t *ctx = tcp_ctx(io);
    if (ctx == NULL) {
        return ESP_OK;
    }
    tcp_close_socket(ctx);
    free(ctx);
    io->transport_ctx = NULL;
    return ESP_OK;
}

static esp_err_t tcp_open(esp_audio_pcm_handle_t io)
{
    return tcp_connect(io);
}

static bool tcp_ensure_connected(esp_audio_pcm_handle_t io)
{
    tcp_ctx_t *ctx = tcp_ctx(io);
    if (ctx == NULL) {
        return false;
    }
    if (ctx->sock >= 0) {
        return true;
    }
    return tcp_connect(io) == ESP_OK;
}

static esp_err_t tcp_close(esp_audio_pcm_handle_t io)
{
    tcp_close_socket(tcp_ctx(io));
    return ESP_OK;
}

static int tcp_read(esp_audio_pcm_handle_t io, void *buf, size_t len, uint32_t timeout_ms)
{
    tcp_ctx_t *ctx = tcp_ctx(io);
    if (ctx == NULL || ctx->sock < 0 || buf == NULL || len == 0) {
        return -1;
    }

    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(ctx->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int n = recv(ctx->sock, buf, len, 0);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        ESP_LOGW(TAG, "TCP peer closed");
        tcp_close_socket(ctx);
        return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
    }
    ESP_LOGW(TAG, "TCP recv failed: errno %d", errno);
    tcp_close_socket(ctx);
    return -1;
}

static int tcp_write(esp_audio_pcm_handle_t io, const void *data, size_t len, uint32_t timeout_ms)
{
    tcp_ctx_t *ctx = tcp_ctx(io);
    if (ctx == NULL || data == NULL || len == 0) {
        return -1;
    }
    if (!tcp_ensure_connected(io)) {
        return -1;
    }

    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(ctx->sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    size_t sent = 0;
    while (sent < len) {
        int n = send(ctx->sock, (const char *)data + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        ESP_LOGW(TAG, "TCP send failed: errno %d", errno);
        tcp_close_socket(ctx);
        return -1;
    }
    return (int)sent;
}

static const esp_audio_pcm_ops_t s_tcp_ops = {
    .init = tcp_init,
    .deinit = tcp_deinit,
    .open = tcp_open,
    .close = tcp_close,
    .read = tcp_read,
    .write = tcp_write,
};

const esp_audio_pcm_ops_t *esp_audio_pcm_ops_tcp(void)
{
    return &s_tcp_ops;
}
