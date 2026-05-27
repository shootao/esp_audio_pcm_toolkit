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

static const char *TAG = "esp_audio_pcm_udp";

typedef struct {
    int sock;
    struct sockaddr_in server_addr;
    bool server_addr_set;
} udp_ctx_t;

static udp_ctx_t *udp_ctx(esp_audio_pcm_handle_t io)
{
    return (udp_ctx_t *)io->transport_ctx;
}

static esp_err_t udp_resolve_server(esp_audio_pcm_handle_t io)
{
    const esp_audio_pcm_udp_config_t *cfg = &io->config.udp;
    udp_ctx_t *ctx = udp_ctx(io);

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
    };
    struct addrinfo *res = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)cfg->pcm_port);

    int err = getaddrinfo(cfg->server_ip, port_str, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "getaddrinfo(%s:%s) failed: %d", cfg->server_ip, port_str, err);
        return ESP_FAIL;
    }

    memset(&ctx->server_addr, 0, sizeof(ctx->server_addr));
    memcpy(&ctx->server_addr, res->ai_addr, res->ai_addrlen);
    ctx->server_addr.sin_port = htons(cfg->pcm_port);
    ctx->server_addr_set = true;
    freeaddrinfo(res);
    return ESP_OK;
}

static esp_err_t udp_init(esp_audio_pcm_handle_t io)
{
    udp_ctx_t *ctx = calloc(1, sizeof(udp_ctx_t));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->sock = -1;
    io->transport_ctx = ctx;
    return ESP_OK;
}

static esp_err_t udp_deinit(esp_audio_pcm_handle_t io)
{
    udp_ctx_t *ctx = udp_ctx(io);
    if (ctx == NULL) {
        return ESP_OK;
    }
    if (ctx->sock >= 0) {
        close(ctx->sock);
        ctx->sock = -1;
    }
    free(ctx);
    io->transport_ctx = NULL;
    return ESP_OK;
}

static esp_err_t udp_open(esp_audio_pcm_handle_t io)
{
    const esp_audio_pcm_udp_config_t *cfg = &io->config.udp;
    udp_ctx_t *ctx = udp_ctx(io);

    if (ctx->sock >= 0) {
        close(ctx->sock);
        ctx->sock = -1;
    }

    ESP_RETURN_ON_ERROR(udp_resolve_server(io), TAG, "resolve server failed");

    ctx->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (ctx->sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(cfg->local_port),
    };
    if (bind(ctx->sock, (struct sockaddr *)&local, sizeof(local)) != 0) {
        ESP_LOGE(TAG, "bind(local_port=%u) failed: errno %d", (unsigned)cfg->local_port, errno);
        close(ctx->sock);
        ctx->sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UDP ready -> %s:%u (local port %u)",
             cfg->server_ip, (unsigned)cfg->pcm_port, (unsigned)cfg->local_port);
    return ESP_OK;
}

static esp_err_t udp_close(esp_audio_pcm_handle_t io)
{
    udp_ctx_t *ctx = udp_ctx(io);
    if (ctx != NULL && ctx->sock >= 0) {
        close(ctx->sock);
        ctx->sock = -1;
    }
    return ESP_OK;
}

static int udp_read(esp_audio_pcm_handle_t io, void *buf, size_t len, uint32_t timeout_ms)
{
    udp_ctx_t *ctx = udp_ctx(io);
    if (ctx == NULL || ctx->sock < 0 || buf == NULL || len == 0) {
        return -1;
    }

    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(ctx->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int n = recvfrom(ctx->sock, buf, len, 0, (struct sockaddr *)&from, &from_len);
    if (n > 0) {
        return n;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    if (n < 0) {
        ESP_LOGW(TAG, "UDP recvfrom failed: errno %d", errno);
    }
    return n;
}

static int udp_write(esp_audio_pcm_handle_t io, const void *data, size_t len, uint32_t timeout_ms)
{
    udp_ctx_t *ctx = udp_ctx(io);
    if (ctx == NULL || ctx->sock < 0 || !ctx->server_addr_set || data == NULL || len == 0) {
        return -1;
    }

    (void)timeout_ms;
    int n = sendto(ctx->sock, data, len, 0,
                   (struct sockaddr *)&ctx->server_addr, sizeof(ctx->server_addr));
    if (n < 0) {
        ESP_LOGW(TAG, "UDP sendto failed: errno %d", errno);
        return -1;
    }
    return n;
}

static const esp_audio_pcm_ops_t s_udp_ops = {
    .init = udp_init,
    .deinit = udp_deinit,
    .open = udp_open,
    .close = udp_close,
    .read = udp_read,
    .write = udp_write,
};

const esp_audio_pcm_ops_t *esp_audio_pcm_ops_udp(void)
{
    return &s_udp_ops;
}
