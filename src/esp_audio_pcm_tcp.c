/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

#include "esp_audio_pcm_priv.h"

static const char *TAG = "esp_audio_pcm_tcp";

#define TCP_TX_TASK_STACK   6144
#define TCP_TX_TASK_PRIO    5
#define TCP_TX_CHUNK        4096
#define TCP_TX_FALLBACK_BUF 8192
#define TCP_SO_SNDBUF       65535

#ifndef CONFIG_ESP_AUDIO_PCM_TCP_TX_BUF_SIZE
#define CONFIG_ESP_AUDIO_PCM_TCP_TX_BUF_SIZE 1048576
#endif

typedef struct {
    int sock;
    uint8_t *tx_buf;
    uint8_t *tx_chunk;
    size_t tx_cap;
    size_t tx_wr;
    size_t tx_rd;
    size_t tx_used;
    size_t tx_ring_high_water;
    uint64_t tx_drop_bytes;
    uint32_t tx_drop_events;
    TickType_t tx_last_drop_log;
    SemaphoreHandle_t tx_mux;
    SemaphoreHandle_t sock_mux;
    TaskHandle_t tx_task;
    volatile bool tx_run;
    esp_audio_pcm_handle_t io;
} tcp_ctx_t;

static tcp_ctx_t *tcp_ctx(esp_audio_pcm_handle_t io)
{
    return (tcp_ctx_t *)io->transport_ctx;
}

/* Snapshot the current socket fd under the lock. The TX task and the control
 * RX task share one socket; the TX task may reconnect at any time, so callers
 * must operate on this snapshot and never on ctx->sock directly. */
static int tcp_get_sock(tcp_ctx_t *ctx)
{
    int sock = -1;
    if (ctx != NULL && ctx->sock_mux != NULL &&
        xSemaphoreTake(ctx->sock_mux, portMAX_DELAY) == pdTRUE) {
        sock = ctx->sock;
        xSemaphoreGive(ctx->sock_mux);
    }
    return sock;
}

static size_t tcp_want_tx_buf_size(const esp_audio_pcm_tcp_config_t *cfg)
{
    size_t want = (cfg != NULL && cfg->tx_buffer_size > 0)
        ? (size_t)cfg->tx_buffer_size
        : (size_t)CONFIG_ESP_AUDIO_PCM_TCP_TX_BUF_SIZE;
    if (want < 4096) {
        want = 4096;
    }
    return want;
}

static uint8_t *tcp_alloc_tx_buf(size_t want, size_t *out_size)
{
#if CONFIG_SPIRAM
    if (esp_psram_is_initialized()) {
        uint8_t *buf = heap_caps_malloc(want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf != NULL) {
            *out_size = want;
            return buf;
        }
        ESP_LOGW(TAG, "PSRAM alloc %u B failed, trying internal fallback", (unsigned)want);
    }
#endif
    uint8_t *buf = heap_caps_malloc(TCP_TX_FALLBACK_BUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buf != NULL) {
        *out_size = TCP_TX_FALLBACK_BUF;
        ESP_LOGW(TAG, "TX ring using %u B internal RAM (enable PSRAM for ~1 MiB buffer)",
                 (unsigned)TCP_TX_FALLBACK_BUF);
    }
    return buf;
}

static size_t ring_free_locked(const tcp_ctx_t *ctx)
{
    return ctx->tx_cap - ctx->tx_used;
}

static size_t ring_write_locked(tcp_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t free = ring_free_locked(ctx);
    if (free == 0) {
        return 0;
    }

    size_t n = len < free ? len : free;
    size_t first = ctx->tx_cap - ctx->tx_wr;
    if (first > n) {
        first = n;
    }
    memcpy(ctx->tx_buf + ctx->tx_wr, data, first);
    if (n > first) {
        memcpy(ctx->tx_buf, data + first, n - first);
    }
    ctx->tx_wr = (ctx->tx_wr + n) % ctx->tx_cap;
    ctx->tx_used += n;
    if (ctx->tx_used > ctx->tx_ring_high_water) {
        ctx->tx_ring_high_water = ctx->tx_used;
    }
    return n;
}

static void tcp_note_drop(tcp_ctx_t *ctx, size_t bytes, const char *reason)
{
    if (ctx == NULL || bytes == 0) {
        return;
    }

    ctx->tx_drop_bytes += (uint64_t)bytes;
    ctx->tx_drop_events++;

    TickType_t now = xTaskGetTickCount();
    bool summary = (ctx->tx_last_drop_log == 0) ||
                   ((now - ctx->tx_last_drop_log) >= pdMS_TO_TICKS(5000));
    if (summary) {
        ESP_LOGW(TAG,
                 "PCM dropped %u B (%s); total %" PRIu64 " B in %u events; "
                 "ring %u/%u B (high water %u B)",
                 (unsigned)bytes, reason,
                 ctx->tx_drop_bytes, (unsigned)ctx->tx_drop_events,
                 (unsigned)ctx->tx_used, (unsigned)ctx->tx_cap,
                 (unsigned)ctx->tx_ring_high_water);
        ctx->tx_last_drop_log = now;
    } else {
        ESP_LOGW(TAG, "PCM dropped %u B (%s)", (unsigned)bytes, reason);
    }
}

static size_t ring_read_locked(tcp_ctx_t *ctx, uint8_t *data, size_t len)
{
    if (ctx->tx_used == 0) {
        return 0;
    }

    size_t n = len < ctx->tx_used ? len : ctx->tx_used;
    size_t first = ctx->tx_cap - ctx->tx_rd;
    if (first > n) {
        first = n;
    }
    memcpy(data, ctx->tx_buf + ctx->tx_rd, first);
    if (n > first) {
        memcpy(data + first, ctx->tx_buf, n - first);
    }
    ctx->tx_rd = (ctx->tx_rd + n) % ctx->tx_cap;
    ctx->tx_used -= n;
    return n;
}

static void tcp_wake_tx_task(tcp_ctx_t *ctx)
{
    if (ctx != NULL && ctx->tx_task != NULL) {
        xTaskNotifyGive(ctx->tx_task);
    }
}

static void tcp_close_socket_locked(tcp_ctx_t *ctx)
{
    if (ctx == NULL || ctx->sock < 0) {
        return;
    }
    shutdown(ctx->sock, SHUT_RDWR);
    close(ctx->sock);
    ctx->sock = -1;
}

static void tcp_close_socket(tcp_ctx_t *ctx)
{
    if (ctx == NULL || ctx->sock_mux == NULL) {
        return;
    }
    xSemaphoreTake(ctx->sock_mux, portMAX_DELAY);
    tcp_close_socket_locked(ctx);
    xSemaphoreGive(ctx->sock_mux);
}

/* Close only if the live socket still matches the snapshot the caller used.
 * Prevents a read/send error from tearing down a socket that the TX task has
 * meanwhile replaced with a fresh connection. */
static void tcp_close_if(tcp_ctx_t *ctx, int sock)
{
    if (ctx == NULL || ctx->sock_mux == NULL || sock < 0) {
        return;
    }
    xSemaphoreTake(ctx->sock_mux, portMAX_DELAY);
    if (ctx->sock == sock) {
        tcp_close_socket_locked(ctx);
    }
    xSemaphoreGive(ctx->sock_mux);
}

static esp_err_t tcp_connect(esp_audio_pcm_handle_t io)
{
    const esp_audio_pcm_tcp_config_t *cfg = &io->config.tcp;
    tcp_ctx_t *ctx = tcp_ctx(io);

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

    int sndbuf = TCP_SO_SNDBUF;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "connect(%s:%s) failed: errno %d", cfg->server_ip, port_str, errno);
        close(sock);
        freeaddrinfo(res);
        return ESP_FAIL;
    }
    freeaddrinfo(res);

    /* Publish the new socket atomically: drop any stale one and install the
     * fresh fd under the lock so concurrent read/send paths never observe a
     * half-open or reused descriptor. */
    xSemaphoreTake(ctx->sock_mux, portMAX_DELAY);
    tcp_close_socket_locked(ctx);
    ctx->sock = sock;
    xSemaphoreGive(ctx->sock_mux);

    ESP_LOGI(TAG, "TCP connected to %s:%u", cfg->server_ip, (unsigned)cfg->port);
    tcp_wake_tx_task(ctx);
    return ESP_OK;
}

static bool tcp_ensure_connected(esp_audio_pcm_handle_t io)
{
    tcp_ctx_t *ctx = tcp_ctx(io);
    if (ctx == NULL) {
        return false;
    }
    if (tcp_get_sock(ctx) >= 0) {
        return true;
    }
    return tcp_connect(io) == ESP_OK;
}

static int tcp_send_chunk(tcp_ctx_t *ctx, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (ctx == NULL || data == NULL || len == 0) {
        return -1;
    }

    int sock = tcp_get_sock(ctx);
    if (sock < 0) {
        return -1;
    }

    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, (const char *)data + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        ESP_LOGW(TAG, "TCP send failed: errno %d", errno);
        tcp_close_if(ctx, sock);
        return -1;
    }
    return (int)sent;
}

static void tcp_tx_task(void *arg)
{
    esp_audio_pcm_handle_t io = (esp_audio_pcm_handle_t)arg;
    tcp_ctx_t *ctx = tcp_ctx(io);
    if (ctx == NULL) {
        vTaskDelete(NULL);
        return;
    }
    if (ctx->tx_chunk == NULL) {
        vTaskDelete(NULL);
        return;
    }

    while (ctx->tx_run) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        if (!ctx->tx_run) {
            break;
        }

        while (ctx->tx_run) {
            if (!tcp_ensure_connected(io)) {
                vTaskDelay(pdMS_TO_TICKS(20));
                break;
            }

            size_t n = 0;
            if (xSemaphoreTake(ctx->tx_mux, pdMS_TO_TICKS(100)) == pdTRUE) {
                n = ring_read_locked(ctx, ctx->tx_chunk, TCP_TX_CHUNK);
                xSemaphoreGive(ctx->tx_mux);
            }
            if (n == 0) {
                break;
            }

            if (tcp_send_chunk(ctx, ctx->tx_chunk, n, 500) < 0) {
                break;
            }
        }
    }

    if (ctx != NULL) {
        ctx->tx_task = NULL;
    }
    vTaskDelete(NULL);
}

static esp_err_t tcp_init(esp_audio_pcm_handle_t io)
{
    tcp_ctx_t *ctx = calloc(1, sizeof(tcp_ctx_t));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->sock = -1;
    ctx->io = io;

    size_t want = tcp_want_tx_buf_size(&io->config.tcp);
    ctx->tx_buf = tcp_alloc_tx_buf(want, &ctx->tx_cap);
    if (ctx->tx_buf == NULL) {
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    ctx->tx_chunk = heap_caps_malloc(TCP_TX_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ctx->tx_chunk == NULL) {
        heap_caps_free(ctx->tx_buf);
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    ctx->tx_mux = xSemaphoreCreateMutex();
    ctx->sock_mux = xSemaphoreCreateMutex();
    if (ctx->tx_mux == NULL || ctx->sock_mux == NULL) {
        if (ctx->tx_mux != NULL) {
            vSemaphoreDelete(ctx->tx_mux);
        }
        if (ctx->sock_mux != NULL) {
            vSemaphoreDelete(ctx->sock_mux);
        }
        heap_caps_free(ctx->tx_chunk);
        heap_caps_free(ctx->tx_buf);
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    ctx->tx_run = true;
    io->transport_ctx = ctx;

    BaseType_t ok = xTaskCreate(tcp_tx_task, "pcm_tcp_tx", TCP_TX_TASK_STACK, io,
                                TCP_TX_TASK_PRIO, &ctx->tx_task);
    if (ok != pdPASS) {
        io->transport_ctx = NULL;
        ctx->tx_run = false;
        vSemaphoreDelete(ctx->tx_mux);
        vSemaphoreDelete(ctx->sock_mux);
        heap_caps_free(ctx->tx_chunk);
        heap_caps_free(ctx->tx_buf);
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_SPIRAM
    const char *where = esp_ptr_external_ram(ctx->tx_buf) ? "PSRAM" : "internal";
#else
    const char *where = "internal";
#endif
    ESP_LOGI(TAG, "TCP TX ring %u B (%s)", (unsigned)ctx->tx_cap, where);
    return ESP_OK;
}

static esp_err_t tcp_deinit(esp_audio_pcm_handle_t io)
{
    tcp_ctx_t *ctx = tcp_ctx(io);
    if (ctx == NULL) {
        return ESP_OK;
    }

    ctx->tx_run = false;
    tcp_close_socket(ctx);
    tcp_wake_tx_task(ctx);
    if (ctx->tx_used > 0) {
        ESP_LOGW(TAG, "TCP deinit with %u B still in TX ring (not sent to PC)",
                 (unsigned)ctx->tx_used);
    }
    if (ctx->tx_drop_bytes > 0) {
        ESP_LOGW(TAG, "TCP session drop stats: %" PRIu64 " B in %u events; ring high water %u B",
                 ctx->tx_drop_bytes, (unsigned)ctx->tx_drop_events,
                 (unsigned)ctx->tx_ring_high_water);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    TickType_t wait_start = xTaskGetTickCount();
    while (ctx->tx_task != NULL) {
        if ((xTaskGetTickCount() - wait_start) > pdMS_TO_TICKS(2000)) {
            ESP_LOGW(TAG, "TX task did not exit cleanly");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (ctx->tx_mux != NULL) {
        vSemaphoreDelete(ctx->tx_mux);
    }
    if (ctx->sock_mux != NULL) {
        vSemaphoreDelete(ctx->sock_mux);
    }
    heap_caps_free(ctx->tx_chunk);
    heap_caps_free(ctx->tx_buf);
    free(ctx);
    io->transport_ctx = NULL;
    return ESP_OK;
}

static esp_err_t tcp_open(esp_audio_pcm_handle_t io)
{
    return tcp_connect(io);
}

static esp_err_t tcp_close(esp_audio_pcm_handle_t io)
{
    tcp_close_socket(tcp_ctx(io));
    return ESP_OK;
}

static int tcp_read(esp_audio_pcm_handle_t io, void *buf, size_t len, uint32_t timeout_ms)
{
    tcp_ctx_t *ctx = tcp_ctx(io);
    if (ctx == NULL || buf == NULL || len == 0) {
        return -1;
    }

    int sock = tcp_get_sock(ctx);
    if (sock < 0) {
        return -1;
    }

    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int n = recv(sock, buf, len, 0);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        ESP_LOGW(TAG, "TCP peer closed");
        tcp_close_if(ctx, sock);
        return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
    }
    ESP_LOGW(TAG, "TCP recv failed: errno %d", errno);
    tcp_close_if(ctx, sock);
    return -1;
}

static int tcp_write(esp_audio_pcm_handle_t io, const void *data, size_t len, uint32_t timeout_ms)
{
    tcp_ctx_t *ctx = tcp_ctx(io);
    if (ctx == NULL || data == NULL || len == 0 || ctx->tx_buf == NULL || ctx->tx_mux == NULL) {
        return -1;
    }
    if (!ctx->tx_run) {
        tcp_note_drop(ctx, len, "transport stopped");
        errno = ENOMEM;
        return -1;
    }

    const uint8_t *src = (const uint8_t *)data;
    size_t written = 0;
    TickType_t start = xTaskGetTickCount();
#if defined(CONFIG_ESP_AUDIO_PCM_TCP_BLOCK_ON_FULL) && CONFIG_ESP_AUDIO_PCM_TCP_BLOCK_ON_FULL
    const bool block_on_full = true;
#else
    const bool block_on_full = false;
#endif
    const TickType_t wait_ticks = block_on_full
        ? portMAX_DELAY
        : ((timeout_ms == 0)
               ? 0
               : pdMS_TO_TICKS(timeout_ms > 0 ? timeout_ms : 5000));

    while (written < len && ctx->tx_run) {
        size_t chunk = 0;
        if (xSemaphoreTake(ctx->tx_mux, pdMS_TO_TICKS(100)) == pdTRUE) {
            chunk = ring_write_locked(ctx, src + written, len - written);
            xSemaphoreGive(ctx->tx_mux);
        }
        if (chunk > 0) {
            written += chunk;
            tcp_wake_tx_task(ctx);
            continue;
        }

        if (!block_on_full) {
            if (wait_ticks == 0) {
                break;
            }
            if ((xTaskGetTickCount() - start) >= wait_ticks) {
                break;
            }
        }
        tcp_wake_tx_task(ctx);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (written == len) {
        return (int)written;
    }

    size_t dropped = len - written;
    if (dropped > 0) {
        const char *reason = ctx->tx_run ? "TX ring full (timeout)" : "transport stopped";
        tcp_note_drop(ctx, dropped, reason);
    }
    if (written > 0) {
        errno = ENOMEM;
        return (int)written;
    }
    errno = ENOMEM;
    return -1;
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
