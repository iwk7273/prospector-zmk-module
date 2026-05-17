/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * See host_rate_rx.h for protocol description.
 *
 * Implementation notes:
 *   - The CDC ACM device behind `zephyr,console` is also our RX endpoint.
 *     Zephyr's console writes use uart_poll_out (TX), and CONFIG_UART_CONSOLE
 *     does not consume RX, so claiming RX via uart_irq_callback_set is safe.
 *   - Parsing runs in the UART IRQ. It only calls meteorite_data setters
 *     (k_spinlock protected) and atomic state flips — no logging, no blocking.
 *   - ping/hello replies are deferred to the system workqueue so printk's
 *     poll-out path does not block the IRQ if the host is slow to drain.
 *   - The cdc_acm_uart device is ready before USB enumerates; IRQs simply
 *     do not fire until the host opens the port (DTR raised).
 */

#include "host_rate_rx.h"
#include "meteorite_data.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(host_rate_rx, CONFIG_ZMK_LOG_LEVEL);

#define UART_RX_TMP_LEN  32

static const struct device *uart_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* Line accumulator. Only touched in IRQ context. */
static char   line_buf[HOST_RATE_RX_LINE_MAX];
static size_t line_len;
static bool   line_overrun;

/* Statistics — accessible via log/debugger; not exported as Kconfig API. */
static uint32_t stat_lines_total;
static uint32_t stat_lines_invalid;
static uint32_t stat_lines_dropped_overrun;
static uint32_t stat_rate_codex;
static uint32_t stat_rate_claude;

/* Deferred reply. IRQ sets pending_reply and submits work; the workqueue
 * thread runs printk safely (no IRQ-context poll-out blocking). */
enum reply_kind { REPLY_NONE, REPLY_PONG, REPLY_HELLO };
static volatile enum reply_kind pending_reply;

static void reply_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    enum reply_kind r = pending_reply;
    pending_reply = REPLY_NONE;
    switch (r) {
    case REPLY_PONG:
        printk("pong meteorite v=1\n");
        break;
    case REPLY_HELLO:
        printk("hello meteorite-rate v1\n");
        break;
    default:
        break;
    }
}
static K_WORK_DEFINE(reply_work, reply_work_handler);

/* ===== Parser helpers ===== */

/* Skip space/tab in place. */
static inline void skip_ws(char **pp) {
    char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    *pp = p;
}

/* Parse a decimal uint at *pp, advancing past it. Rejects empty / overflow. */
static bool parse_uint(char **pp, uint32_t *out, uint32_t max) {
    char *p = *pp;
    skip_ws(&p);
    if (*p < '0' || *p > '9') return false;
    uint64_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint64_t)(*p - '0');
        if (v > (uint64_t)max) return false;
        p++;
    }
    *out = (uint32_t)v;
    *pp = p;
    return true;
}

/* ===== Command handlers ===== */

static void handle_rate(char *args) {
    /* args points at the first character after "rate ". Extract source. */
    skip_ws(&args);
    char *src_str = args;
    while (*args && *args != ' ' && *args != '\t') args++;
    if (!*args) { stat_lines_invalid++; return; }
    *args++ = '\0';

    enum meteorite_rate_source src;
    if (strcmp(src_str, "codex") == 0) {
        src = METEORITE_RATE_CODEX;
    } else if (strcmp(src_str, "claude") == 0) {
        src = METEORITE_RATE_CLAUDE;
    } else {
        stat_lines_invalid++;
        return;
    }

    uint32_t pct5h, pctW, sec5h, secW;
    if (!parse_uint(&args, &pct5h, 100))          { stat_lines_invalid++; return; }
    if (!parse_uint(&args, &pctW,  100))          { stat_lines_invalid++; return; }
    if (!parse_uint(&args, &sec5h, UINT32_MAX))   { stat_lines_invalid++; return; }
    if (!parse_uint(&args, &secW,  UINT32_MAX))   { stat_lines_invalid++; return; }

    meteorite_data_set_rate_limit(src,
        (uint8_t)pct5h, (uint8_t)pctW, sec5h, secW);

    if (src == METEORITE_RATE_CODEX) {
        stat_rate_codex++;
    } else {
        stat_rate_claude++;
    }
}

static void handle_line(char *line) {
    /* Trim trailing CR / whitespace in place. */
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' ||
                     line[n - 1] == ' '  ||
                     line[n - 1] == '\t')) {
        line[--n] = '\0';
    }
    /* Skip leading whitespace. */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0') return;

    if (strncmp(line, "rate ", 5) == 0) {
        handle_rate(line + 5);
    } else if (strcmp(line, "ping") == 0) {
        pending_reply = REPLY_PONG;
        k_work_submit(&reply_work);
    } else if (strcmp(line, "hello") == 0) {
        pending_reply = REPLY_HELLO;
        k_work_submit(&reply_work);
    } else {
        stat_lines_invalid++;
        /* Avoid log spam from random terminal noise; only DBG. */
    }
}

/* ===== UART IRQ ===== */

static void uart_isr(const struct device *dev, void *user_data) {
    ARG_UNUSED(user_data);
    uint8_t buf[UART_RX_TMP_LEN];

    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (!uart_irq_rx_ready(dev)) break;

        int n = uart_fifo_read(dev, buf, sizeof(buf));
        if (n <= 0) continue;

        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];

            if (c == '\n') {
                stat_lines_total++;
                if (line_overrun) {
                    stat_lines_dropped_overrun++;
                } else {
                    line_buf[line_len] = '\0';
                    handle_line(line_buf);
                }
                line_len = 0;
                line_overrun = false;
                continue;
            }

            if (line_overrun) {
                continue;  /* swallow until terminator */
            }
            if (line_len >= HOST_RATE_RX_LINE_MAX - 1) {
                line_overrun = true;
                continue;
            }
            line_buf[line_len++] = c;
        }
    }
}

/* ===== Init ===== */

int host_rate_rx_init(void) {
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("host_rate_rx: zephyr,console device not ready");
        return -ENODEV;
    }

    uart_irq_rx_disable(uart_dev);
    uart_irq_tx_disable(uart_dev);
    uart_irq_callback_set(uart_dev, uart_isr);
    uart_irq_rx_enable(uart_dev);

    LOG_INF("host_rate_rx listening on zephyr,console (CDC ACM)");
    return 0;
}

SYS_INIT(host_rate_rx_init, APPLICATION, 95);
