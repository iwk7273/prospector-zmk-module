/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Meteorite Layout data aggregator.
 *
 * Single source of truth for the Meteorite status display. Three independent
 * data sources feed into this store, and the layout pulls a consistent
 * snapshot from it on every render tick.
 *
 *   BLE ADV v1 (legacy 26-byte, service UUID 0xABCD; receiver:
 *               scanner_stub.c -> bootstrap.c::apply_pending_to_data):
 *     - keyboard name, central + peripheral battery, active layer index,
 *       4-char active layer name, modifier flags, connection pills, RSSI.
 *
 *   BLE ADV v2 multi-frame (service UUID 0xABCE; receiver:
 *               scanner_stub.c::scanner_pop_pending_v2 ->
 *               bootstrap.c::apply_v2_frame):
 *     - full layer name list (count + names[10][5])
 *     - Meteorite custom config (OS mode, CPI, scroll layer indices)
 *     v2 fields are NOT aged out independently — the keyboard's idle-mode
 *     v2 cadence is too slow (~5 min between frames) for any short
 *     threshold to work. has_layer_list / has_custom_config are cleared
 *     atomically with has_keyboard on v1 timeout (~8 min default).
 *
 *   Host PC USB CDC line protocol (host_rate_rx.c, shares zephyr,console
 *               CDC ACM with logging):
 *     - Codex / Claude rate-limit window usage and reset epochs.
 *
 * Setters are called from the LVGL timer / scanner work handler / UART IRQ
 * contexts. The layout reads via meteorite_data_snapshot() which copies
 * the whole struct under a spinlock.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METEORITE_LAYER_NAME_LEN     5    /* 4 chars + NUL */
#define METEORITE_MAX_LAYERS         10
#define METEORITE_KB_NAME_LEN        24
#define METEORITE_PERIPHERAL_COUNT   3

enum meteorite_os_mode {
    METEORITE_OS_WIN = 0,
    METEORITE_OS_MAC = 1,
    METEORITE_OS_UNKNOWN = 0xFF,
};

enum meteorite_rate_source {
    METEORITE_RATE_CODEX  = 0,
    METEORITE_RATE_CLAUDE = 1,
    METEORITE_RATE_COUNT,
};

/* The scanner has no RTC, so the PC daemon sends "seconds until reset" rather
 * than wall-clock epoch. The renderer recomputes remaining seconds against
 * captured_at_ms to age the value naturally between updates. */
struct meteorite_rate_limit {
    bool     valid;            /* false until first sample received */
    uint8_t  pct_5h;           /* 0-100 */
    uint8_t  pct_weekly;       /* 0-100 */
    uint32_t sec_until_5h;     /* as captured */
    uint32_t sec_until_w;
    uint32_t captured_at_ms;   /* k_uptime_get_32() at last set */
};

struct meteorite_snapshot {
    /* === Keyboard identity / connection (ADV v1) === */
    bool     has_keyboard;
    char     keyboard_name[METEORITE_KB_NAME_LEN];
    int8_t   rssi_dbm;
    uint8_t  battery_central;            /* 0-100, 0=unknown */
    uint8_t  battery_peripheral[METEORITE_PERIPHERAL_COUNT];

    bool     usb_connected;
    bool     ble_connected;
    bool     ble_bonded;
    uint8_t  ble_profile_slot;           /* 0-4 */

    /* === Active layer (ADV v1) === */
    uint8_t  active_layer;
    char     active_layer_name[METEORITE_LAYER_NAME_LEN];

    /* === Layer name list (ADV v2) === */
    bool     has_layer_list;
    uint8_t  layer_count;
    char     layer_names[METEORITE_MAX_LAYERS][METEORITE_LAYER_NAME_LEN];

    /* === Meteorite custom config (ADV v2) === */
    bool     has_custom_config;
    uint8_t  os_mode;                    /* enum meteorite_os_mode */
    uint16_t cpi_value;                  /* actual DPI (e.g. 800) */
    uint8_t  scroll_layer_1;             /* layer index, 0xFF=disabled */
    uint8_t  scroll_layer_2;
    uint16_t scroll_div_value;

    /* === Host rate limits (USB CDC) === */
    struct meteorite_rate_limit rate[METEORITE_RATE_COUNT];
};

void meteorite_data_init(void);

/* Take a consistent snapshot for rendering. */
void meteorite_data_snapshot(struct meteorite_snapshot *out);

/* Setter — called from BLE ADV v1 receive path. */
void meteorite_data_set_keyboard_v1(
    const char *name,
    int8_t rssi,
    uint8_t battery_central,
    const uint8_t peripheral_battery[METEORITE_PERIPHERAL_COUNT],
    uint8_t active_layer,
    const char active_layer_name[METEORITE_LAYER_NAME_LEN],
    bool usb_connected,
    bool ble_connected,
    bool ble_bonded,
    uint8_t ble_profile_slot
);

/* Setter — called from BLE ADV v2 receive path. */
void meteorite_data_set_layer_list(uint8_t count,
                                   const char names[][METEORITE_LAYER_NAME_LEN]);

void meteorite_data_set_custom_config(uint8_t os_mode,
                                      uint16_t cpi_value,
                                      uint8_t scroll_layer_1,
                                      uint8_t scroll_layer_2,
                                      uint16_t scroll_div_value);

/* Setter — called from host CDC receive path (host_rate_rx.c, UART IRQ). */
void meteorite_data_set_rate_limit(enum meteorite_rate_source src,
                                   uint8_t pct_5h, uint8_t pct_weekly,
                                   uint32_t sec_until_reset_5h,
                                   uint32_t sec_until_reset_w);

/* Mark keyboard as disconnected (all ADV-derived fields go invalid). */
void meteorite_data_clear_keyboard(void);

#ifdef __cplusplus
}
#endif
