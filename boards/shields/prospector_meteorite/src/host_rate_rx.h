/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Phase 3a — Host PC rate-limit ingress over USB CDC ACM.
 *
 * Multiplexes onto the existing `zephyr,console` CDC ACM device:
 *   - device -> host: continues to carry Zephyr logs (TX-only path)
 *   - host -> device: lines are parsed by this module and dispatched
 *     to meteorite_data setters.
 *
 * Line protocol (terminated by '\n'; preceding '\r' tolerated):
 *
 *   rate <source> <pct5h> <pctW> <sec5h> <secW>
 *       source: "codex" | "claude"
 *       pct5h, pctW: 0..100 (decimal)
 *       sec5h, secW: 0..UINT32_MAX seconds until reset (decimal)
 *
 *   ping                              -> "pong meteorite v=1\n"
 *   hello                             -> "hello meteorite-rate v1\n"
 *
 * Unknown lines are silently dropped (LOG_DBG + invalid counter).
 * Lines longer than HOST_RATE_RX_LINE_MAX are dropped on the next '\n'.
 *
 * SYS_INIT registers the UART IRQ callback at APPLICATION priority 95
 * (after display init); the host can open the port at any time after that.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_RATE_RX_LINE_MAX  96

/* Optional: callers may invoke explicitly, but SYS_INIT handles the
 * normal boot path so app code does not need to do anything. */
int host_rate_rx_init(void);

#ifdef __cplusplus
}
#endif
