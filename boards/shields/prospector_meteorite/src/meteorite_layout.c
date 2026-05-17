/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Meteorite Layout — see meteorite_layout.h for zone layout.
 *
 * All widgets are placed with absolute coordinates. No flex/grid containers,
 * matching the existing Prospector design principle (custom_status_screen.c
 * comment block). Widgets are owned by this translation unit; meteorite_layout
 * does not retain a parent reference after create().
 */

#include "meteorite_layout.h"
#include "meteorite_data.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(meteorite_layout, CONFIG_ZMK_LOG_LEVEL);

/* ========== Geometry ========== */

#define SCREEN_W  280
#define SCREEN_H  240

#define HDR_Y     0
#define HDR_H     36

/* Layer zone absorbs the former CFG strip (CPI/SCRL moved into the layer
 * panel). The rate zone now pushes up to LAYER_Y+LAYER_H so the
 * Codex/Claude bars get visual room (~25 extra px vs the previous
 * RATE_Y=148). */
#define LAYER_Y   36
#define LAYER_H   88

#define RATE_Y    (LAYER_Y + LAYER_H)
#define RATE_H    (SCREEN_H - RATE_Y)

/* Layer zone is split into a left "big layer label" panel and a right
 * connectivity-status panel (OS icons + BLE + USB pills). Left panel
 * narrowed to 158 so the right panel can fit BLE and USB pills
 * side-by-side (was vertically stacked). */
#define LAYER_LEFT_W   158
#define LAYER_DIV_X    (LAYER_LEFT_W + 2)
#define LAYER_RIGHT_X  (LAYER_DIV_X + 6)

/* Bar corner radii. Main rate bar is 10px tall → radius 5 = pill ends.
 * RSSI signal-bars don't use a bar widget anymore; phone-style ascending
 * rectangles instead. */
#define BAR_RATE_RADIUS   5

/* Pace tick (vertical line at elapsed-ratio of the window) replaces the
 * earlier thin horizontal pace bar — a single tick reads as a "you
 * should be here" marker against the main usage bar. */
#define PACE_TICK_W   2
#define PACE_TICK_H   14

/* Phone-style RSSI signal icon: 5 ascending vertical bars. Heights
 * 3,5,7,9,11px; each 3px wide with 2px gaps. Total footprint ~23×11px. */
#define SIGNAL_BAR_COUNT  5
#define SIGNAL_BAR_W      3
#define SIGNAL_BAR_GAP    2

/* v2 ext-adv fields are NOT aged out independently of v1: the keyboard's
 * v2 cadence drops to ~1 frame per 5 min (per-frame_id ~15 min) once it
 * goes idle (Kconfig PROSPECTOR_STATUS_ADV_V2_INTERVAL × idle tick rate),
 * so any short staleness threshold would dim CPI/SCRL/OS the moment the
 * user stops typing. Instead, has_custom_config / has_layer_list track
 * "received at least once" and are cleared atomically with has_keyboard
 * by meteorite_data_clear_keyboard() on v1 timeout (~8 min). */

/* Rate-limit values arrive from the host PC daemon at a 5-minute cadence
 * (matching claude-usage-widget's polling interval). Hold them on screen
 * for 10 minutes so a single missed poll (laptop briefly asleep, transient
 * network blip) does not flicker the row back to "--". After 10 minutes of
 * silence, treat the daemon as dead and surface that as "--". */
#define RATE_STALE_MS  (10u * 60u * 1000u)

/* Window lengths the daemon polls against. Used to derive elapsed-ratio
 * (pace) from the host-supplied "seconds until reset". */
#define WINDOW_5H_SEC   (5u * 3600u)
#define WINDOW_W_SEC    (7u * 24u * 3600u)

/* Pace coloring thresholds (used_pct - elapsed_pct, signed percent-points).
 * Under -5: comfortably ahead (green). -5..+10: roughly on pace (yellow).
 * >+10: outpacing the clock, likely to exhaust the window early (red).
 * Below WARMUP_ELAPSED_PCT% elapsed, force green — early in the window
 * even a small absolute usage looks "over pace" but the signal is noise. */
#define PACE_OVER_PP        10
#define PACE_AHEAD_PP       (-5)
#define WARMUP_ELAPSED_PCT  5u

/* ========== Colors (single palette for Phase 1) ========== */

#define COL_BG          lv_color_black()
#define COL_HDR_BG      lv_color_hex(0x0c0c10)
#define COL_FG          lv_color_hex(0xe0e0e6)
#define COL_DIM         lv_color_hex(0x6a6a72)
#define COL_INACTIVE    lv_color_hex(0x383840)
#define COL_ACCENT      lv_color_hex(0x4DABF7)
#define COL_GOOD        lv_color_hex(0x69DB7C)
#define COL_WARN        lv_color_hex(0xFFD43B)
#define COL_BAD         lv_color_hex(0xFF6B6B)
#define COL_SEP         lv_color_hex(0x2a2a32)
/* Brand-ish source colors for the rate-source icons (image_recolor tint). */
#define COL_CODEX       lv_color_hex(0x10A37F)  /* OpenAI / ChatGPT green */
#define COL_CLAUDE      lv_color_hex(0xD97757)  /* Anthropic terracotta   */

/* ========== Widget handles ========== */

static lv_obj_t *root             = NULL;

/* Header */
static lv_obj_t *lbl_kb_name      = NULL;
static lv_obj_t *lbl_rssi_dbm     = NULL;
static lv_obj_t *signal_bars[SIGNAL_BAR_COUNT] = {0};  /* phone-style 5-bar */
static lv_obj_t *lbl_battery_pct  = NULL;

/* Layer zone — left half: big "L0 BASE" + CPI/SCRL on a sub-row */
static lv_obj_t *lbl_layer_main   = NULL;
static lv_obj_t *lbl_cpi          = NULL;
static lv_obj_t *lbl_scrl         = NULL;

/* Layer zone — right half: OS icons (active/inactive tint) and the
 * BLE/USB pills laid out horizontally side-by-side. */
static lv_obj_t *img_os_win       = NULL;
static lv_obj_t *img_os_mac       = NULL;
static lv_obj_t *lbl_ble_pill     = NULL;
static lv_obj_t *lbl_usb_pill     = NULL;

/* ====== OS icons — 24×24 1bpp, generated by _gen_rate_icons.py ======
 * LVGL v9 renders LV_COLOR_FORMAT_A1 through the widget's image_recolor,
 * so the icons inherit the text color set on the lv_image widget. Bit
 * order is MSB-first within each byte; row stride is 3 bytes (24 px / 8). */

static const uint8_t os_icon_windows_map[] = {
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0x00, 0x00, 0x00,  /*                          */
    0x00, 0x00, 0x00,  /*                          */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
    0xFF, 0xE7, 0xFF,  /* ###########  ########### */
};

static const lv_image_dsc_t os_icon_windows = {
    .header = {
        .magic  = LV_IMAGE_HEADER_MAGIC,
        .cf     = LV_COLOR_FORMAT_A1,
        .flags  = 0,
        .w      = 24,
        .h      = 24,
        .stride = 3,
    },
    .data_size = sizeof(os_icon_windows_map),
    .data = os_icon_windows_map,
};

static const uint8_t os_icon_apple_map[] = {
    0x00, 0x00, 0x00,  /*                          */
    0x00, 0x01, 0x00,  /*                #         */
    0x00, 0x07, 0x00,  /*              ###         */
    0x00, 0x07, 0x00,  /*              ###         */
    0x00, 0x0E, 0x00,  /*             ###          */
    0x00, 0x0C, 0x00,  /*             ##           */
    0x01, 0xC1, 0xC0,  /*        ###     ###       */
    0x07, 0xFF, 0xF0,  /*      ###############     */
    0x0F, 0xFF, 0xF0,  /*     ################     */
    0x0F, 0xFF, 0xE0,  /*     ###############      */
    0x1F, 0xFF, 0xE0,  /*    ################      */
    0x1F, 0xFF, 0xC0,  /*    ###############       */
    0x1F, 0xFF, 0xC0,  /*    ###############       */
    0x1F, 0xFF, 0xC0,  /*    ###############       */
    0x1F, 0xFF, 0xC0,  /*    ###############       */
    0x1F, 0xFF, 0xE0,  /*    ################      */
    0x0F, 0xFF, 0xF0,  /*     ################     */
    0x0F, 0xFF, 0xF8,  /*     #################    */
    0x0F, 0xFF, 0xF0,  /*     ################     */
    0x07, 0xFF, 0xF0,  /*      ###############     */
    0x03, 0xFF, 0xE0,  /*       #############      */
    0x03, 0xFF, 0xE0,  /*       #############      */
    0x01, 0xE3, 0x80,  /*        ####   ###        */
    0x00, 0x00, 0x00,  /*                          */
};

static const lv_image_dsc_t os_icon_apple = {
    .header = {
        .magic  = LV_IMAGE_HEADER_MAGIC,
        .cf     = LV_COLOR_FORMAT_A1,
        .flags  = 0,
        .w      = 24,
        .h      = 24,
        .stride = 3,
    },
    .data_size = sizeof(os_icon_apple_map),
    .data = os_icon_apple_map,
};

/* ====== Rate-source icons — 32x32 1bpp, generated by _gen_rate_icons.py.
 * Spans both window rows (5h + weekly) at the left of the rate zone, so
 * one icon = one source. Rendered via image_recolor like the OS icons. */

static const uint8_t rate_icon_claude_map[] = {
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x07, 0xFF, 0xFF, 0xE0,  /*      ######################      */
    0x07, 0xFF, 0xFF, 0xE0,  /*      ######################      */
    0x07, 0xFF, 0xFF, 0xE0,  /*      ######################      */
    0x07, 0xFF, 0xFF, 0xE0,  /*      ######################      */
    0x07, 0xBF, 0xFD, 0xE0,  /*      #### ############ ####      */
    0x07, 0xBF, 0xFD, 0xE0,  /*      #### ############ ####      */
    0x07, 0xBF, 0xFD, 0xE0,  /*      #### ############ ####      */
    0x07, 0xBF, 0xFD, 0xE0,  /*      #### ############ ####      */
    0x7F, 0xFF, 0xFF, 0xFE,  /*  ##############################  */
    0x7F, 0xFF, 0xFF, 0xFE,  /*  ##############################  */
    0x7F, 0xFF, 0xFF, 0xFE,  /*  ##############################  */
    0x7F, 0xFF, 0xFF, 0xFE,  /*  ##############################  */
    0x07, 0xFF, 0xFF, 0xE0,  /*      ######################      */
    0x07, 0xFF, 0xFF, 0xE0,  /*      ######################      */
    0x07, 0xFF, 0xFF, 0xE0,  /*      ######################      */
    0x01, 0xB0, 0x0D, 0x80,  /*        ## ##        ## ##        */
    0x01, 0xB0, 0x0D, 0x80,  /*        ## ##        ## ##        */
    0x01, 0xB0, 0x0D, 0x80,  /*        ## ##        ## ##        */
    0x01, 0xB0, 0x0D, 0x80,  /*        ## ##        ## ##        */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
};

static const lv_image_dsc_t rate_icon_claude = {
    .header = {
        .magic  = LV_IMAGE_HEADER_MAGIC,
        .cf     = LV_COLOR_FORMAT_A1,
        .flags  = 0,
        .w      = 32,
        .h      = 32,
        .stride = 4,
    },
    .data_size = sizeof(rate_icon_claude_map),
    .data = rate_icon_claude_map,
};

static const uint8_t rate_icon_codex_map[] = {
    0x00, 0x00, 0x00, 0x00,  /*                                  */
    0x00, 0x1F, 0x80, 0x00,  /*            ######                */
    0x00, 0x3F, 0xE0, 0x00,  /*           #########              */
    0x00, 0x78, 0xFF, 0x00,  /*          ####   ########         */
    0x00, 0xE0, 0x7F, 0xC0,  /*         ###      #########       */
    0x01, 0xC0, 0xFB, 0xE0,  /*        ###      ##### #####      */
    0x03, 0x83, 0xC0, 0x70,  /*       ###     ####       ###     */
    0x0F, 0x8F, 0x80, 0x38,  /*     #####   #####         ###    */
    0x1F, 0x9E, 0x1C, 0x18,  /*    ######  ####    ###     ##    */
    0x39, 0x98, 0x3E, 0x18,  /*   ###  ##  ##     #####    ##    */
    0x31, 0x98, 0xE7, 0x98,  /*   ##   ##  ##   ###  ####  ##    */
    0x71, 0x9B, 0xC3, 0xF8,  /*  ###   ##  ## ####    #######    */
    0x61, 0x9F, 0xE0, 0xF8,  /*  ##    ##  ########     #####    */
    0x61, 0x9C, 0x38, 0x38,  /*  ##    ##  ###    ###     ###    */
    0x61, 0x98, 0x1E, 0x1C,  /*  ##    ##  ##      ####    ###   */
    0x71, 0x98, 0x1F, 0x8C,  /*  ###   ##  ##      ######   ##   */
    0x31, 0xF8, 0x19, 0x8E,  /*   ##   ######      ##  ##   ###  */
    0x38, 0x78, 0x19, 0x86,  /*   ###    ####      ##  ##    ##  */
    0x1C, 0x1C, 0x39, 0x86,  /*    ###     ###    ###  ##    ##  */
    0x1F, 0x07, 0xF9, 0x86,  /*    #####     ########  ##    ##  */
    0x1F, 0xC3, 0xD9, 0x8E,  /*    #######    #### ##  ##   ###  */
    0x19, 0xE7, 0x19, 0x8C,  /*    ##  ####  ###   ##  ##   ##   */
    0x18, 0x7C, 0x19, 0x9C,  /*    ##    #####     ##  ##  ###   */
    0x18, 0x38, 0x79, 0xF8,  /*    ##     ###    ####  ######    */
    0x1C, 0x01, 0xF1, 0xF0,  /*    ###         #####   #####     */
    0x0E, 0x03, 0xC1, 0xC0,  /*     ###       ####     ###       */
    0x07, 0xDF, 0x03, 0x80,  /*      ##### #####      ###        */
    0x03, 0xFE, 0x07, 0x00,  /*       #########      ###         */
    0x00, 0xFE, 0x0E, 0x00,  /*         #######     ###          */
    0x00, 0x07, 0xFC, 0x00,  /*              #########           */
    0x00, 0x03, 0xF8, 0x00,  /*               #######            */
    0x00, 0x00, 0x00, 0x00,  /*                                  */
};

static const lv_image_dsc_t rate_icon_codex = {
    .header = {
        .magic  = LV_IMAGE_HEADER_MAGIC,
        .cf     = LV_COLOR_FORMAT_A1,
        .flags  = 0,
        .w      = 32,
        .h      = 32,
        .stride = 4,
    },
    .data_size = sizeof(rate_icon_codex_map),
    .data = rate_icon_codex_map,
};

/* Rate zone (2 sources × 2 rows). The source label is now a 24x24 icon
 * spanning both rows (icon_src[src]) — no per-row src label anymore.
 * bar_pace_tick is a vertical line, sliding along the bar at the
 * elapsed-ratio position — a "you should be here" marker.
 *
 * ETA is split into two right-aligned sub-labels so the unit suffixes
 * line up vertically across rows in tabular fashion:
 *   label_eta_a holds the leading unit ("23h" / "6d" / "") — its right
 *               edge anchors the first column, so 'h' and 'd' across
 *               rows land in the same x.
 *   label_eta_b holds the trailing unit ("59m" / "23h" / "30m") — its
 *               right edge anchors the second column, aligning 'm'/'h'.
 * A single right-aligned label would only align the very last char, so
 * "30m" and "23h 59m" would have 'm' aligned but 'h' (of 59m's "59") and
 * '3' (of "23h") would not. */
struct rate_row_widgets {
    lv_obj_t *bar_pace_tick;
    lv_obj_t *bar;
    lv_obj_t *label_prefix;  /* "H:" / "W:" — set once at build, never changes */
    lv_obj_t *label_pct;
    lv_obj_t *label_eta_a;   /* first unit:  "23h" / "6d"  / "" */
    lv_obj_t *label_eta_b;   /* second unit: "59m" / "23h" / "30m" / "--" */
};
static struct rate_row_widgets rate_rows[METEORITE_RATE_COUNT][2];
static lv_obj_t *icon_src[METEORITE_RATE_COUNT];

/* ========== Helpers ========== */

static uint8_t rssi_to_bars(int8_t r) {
    if (r >= -50) return 5;
    if (r >= -60) return 4;
    if (r >= -70) return 3;
    if (r >= -80) return 2;
    if (r >= -90) return 1;
    return 0;
}

/* Color the main % bar by *pace* (used vs elapsed) rather than absolute
 * usage. Mirrors CodexBar's deficit / on-pace / reserve semantics: if you
 * are out-spending the clock you'll exhaust the window early — that's the
 * actionable signal, not raw "75% used".
 *
 * Always red once usage is at or above 95% regardless of elapsed — at that
 * point the window is effectively burned and pace becomes irrelevant. */
static lv_color_t pace_color(uint8_t used_pct, uint8_t elapsed_pct) {
    if (used_pct >= 95) return COL_BAD;
    if (elapsed_pct < WARMUP_ELAPSED_PCT) return COL_GOOD;
    int dev = (int)used_pct - (int)elapsed_pct;
    if (dev >= PACE_OVER_PP)  return COL_BAD;
    if (dev >= PACE_AHEAD_PP) return COL_WARN;
    return COL_GOOD;
}

/* Derive elapsed-ratio (percent) for a single window from "seconds until
 * the window resets" — total - remaining, clamped to 0..100. */
static uint8_t elapsed_pct_from_remaining(uint32_t remaining_s,
                                          uint32_t total_s) {
    if (total_s == 0) return 0;
    uint32_t r = remaining_s > total_s ? total_s : remaining_s;
    uint32_t elapsed = total_s - r;
    uint32_t pct = (elapsed * 100u + total_s / 2u) / total_s;
    return pct > 100u ? 100u : (uint8_t)pct;
}

static lv_color_t battery_color(uint8_t pct) {
    if (pct == 0) return COL_INACTIVE;
    if (pct < 20) return COL_BAD;
    if (pct < 40) return COL_WARN;
    return COL_GOOD;
}

/* Format remaining seconds into two parts (leading-unit / trailing-unit),
 * picked so the largest non-zero unit always leads (no "0h 30m" / "0d 5h"):
 *   >= 1d    a="Xd"  b="Yh"     (weekly window: max "6d" / "23h")
 *   >= 1h    a="Xh"  b="Ym"     (max "23h" / "59m")
 *   <  1h    a=""    b="Xm"     ("30m"; collapses the 0-hour case)
 *   == 0     a=""    b="--"     stale / no-data placeholder
 * The split exists so two right-aligned LVGL slots can column-align the
 * unit suffixes across rows ('h'/'d' in column A, 'm'/'h' in column B).
 * Trailing minute is %u (not %02u): "1h 5m" reads as a duration; "1h 05m"
 * looks like clock time again. */
static void fmt_eta_parts(char *a, size_t asz, char *b, size_t bsz,
                          uint32_t remaining_s) {
    if (remaining_s == 0) {
        a[0] = '\0';
        snprintf(b, bsz, "--");
        return;
    }
    if (remaining_s >= 86400u) {
        uint32_t d = remaining_s / 86400u;
        uint32_t h = (remaining_s % 86400u) / 3600u;
        snprintf(a, asz, "%ud", (unsigned)d);
        snprintf(b, bsz, "%uh", (unsigned)h);
    } else if (remaining_s >= 3600u) {
        uint32_t h = remaining_s / 3600u;
        uint32_t m = (remaining_s % 3600u) / 60u;
        snprintf(a, asz, "%uh", (unsigned)h);
        snprintf(b, bsz, "%um", (unsigned)m);
    } else {
        uint32_t m = remaining_s / 60u;
        a[0] = '\0';
        snprintf(b, bsz, "%um", (unsigned)m);
    }
}

/* ========== Builders ========== */

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y,
                            const lv_font_t *font, lv_color_t color,
                            const char *initial) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, initial);
    return l;
}

/* Right-aligned numeric label used by the rate rows for pct ("23%") and
 * ETA ("1h 23m"). Fixed-width slot keeps the trailing digit/unit stable
 * as the value width changes. font 16 / COL_DIM until refresh() promotes
 * it to COL_FG when fresh data arrives.
 *
 * LV_LABEL_LONG_CLIP is required: LVGL's default LONG_WRAP makes the label
 * collapse to content width when text fits, leaving LV_TEXT_ALIGN_RIGHT
 * with nothing to align against — short ETAs ("30m") would anchor their
 * left edge at x, long ones ("23h 59m") would extend further right, so
 * the rate rows' right edges would never line up across sources. */
static lv_obj_t *make_rate_value_label(lv_obj_t *parent, int x, int y,
                                       int w, const char *initial) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, 18);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, COL_DIM, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(l, initial);
    return l;
}

static void build_header(lv_obj_t *p) {
    /* Header background strip (a thin label-less rect via a static lv_obj) */
    lv_obj_t *strip = lv_obj_create(p);
    lv_obj_remove_style_all(strip);
    lv_obj_set_pos(strip, 0, HDR_Y);
    lv_obj_set_size(strip, SCREEN_W, HDR_H);
    lv_obj_set_style_bg_color(strip, COL_HDR_BG, 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);

    lbl_kb_name = make_label(p, 12, HDR_Y + 10,
                             &lv_font_montserrat_16, COL_FG, "----");

    /* RSSI block packed against the battery readout on the right, leaving
     * the entire left side for the keyboard name. "-127 dBm" at font 14
     * is ~70 px; widen the label to 76 and right-align so trailing digits
     * stay flush against the signal-bars icon. */
    lbl_rssi_dbm = lv_label_create(p);
    lv_obj_set_pos(lbl_rssi_dbm, 124, HDR_Y + 11);
    lv_obj_set_size(lbl_rssi_dbm, 76, 16);
    lv_label_set_long_mode(lbl_rssi_dbm, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(lbl_rssi_dbm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_rssi_dbm, COL_FG, 0);
    lv_obj_set_style_text_align(lbl_rssi_dbm, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_rssi_dbm, "-- dBm");

    /* Phone-style 5-bar signal icon: ascending heights, bottom-aligned at
     * a common baseline so the bars look like a staircase. Each bar gets
     * tinted GOOD/WARN/BAD by refresh() based on rssi_to_bars(); unfilled
     * bars stay COL_INACTIVE so the icon is always 5 bars wide regardless
     * of signal strength. */
    int sig_x      = 204;
    int sig_baseline_y = HDR_Y + 26;          /* common bottom edge */
    for (int i = 0; i < SIGNAL_BAR_COUNT; i++) {
        int h = 3 + i * 2;                    /* 3, 5, 7, 9, 11 */
        int x = sig_x + i * (SIGNAL_BAR_W + SIGNAL_BAR_GAP);
        int y = sig_baseline_y - h;
        lv_obj_t *b = lv_obj_create(p);
        lv_obj_remove_style_all(b);
        lv_obj_set_pos(b, x, y);
        lv_obj_set_size(b, SIGNAL_BAR_W, h);
        lv_obj_set_style_bg_color(b, COL_INACTIVE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 1, 0);
        signal_bars[i] = b;
    }

    /* Battery % right-aligned at the right edge so the trailing digit holds
     * a stable position as the value moves between "5%" and "100%". Fixed
     * width 36 = font 16 "100%" worst case; x=232 anchors the right edge at
     * 268, mirroring the 12-px left margin used by the keyboard name. */
    lbl_battery_pct = lv_label_create(p);
    lv_obj_set_pos(lbl_battery_pct, 232, HDR_Y + 10);
    lv_obj_set_size(lbl_battery_pct, 36, 18);
    lv_label_set_long_mode(lbl_battery_pct, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(lbl_battery_pct, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_battery_pct, COL_FG, 0);
    lv_obj_set_style_text_align(lbl_battery_pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_battery_pct, "--%");
}

static void build_layer(lv_obj_t *p) {
    /* "L0 BASE" — left-aligned in the layer zone's narrower left panel.
     * Font_28 with the 6-char-max layer-name policy fits "L9 LOWER"
     * (~144 px) inside LAYER_LEFT_W=158 with a 14 px right margin. */
    lbl_layer_main = lv_label_create(p);
    lv_obj_set_style_text_font(lbl_layer_main, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_layer_main, COL_FG, 0);
    lv_label_set_text(lbl_layer_main, "L- ----");
    lv_obj_set_pos(lbl_layer_main, 14, LAYER_Y + 6);

    /* CPI + SCRL share a single row below the layer label.
     * "CPI 3200" ~64 px + gap + "SCRL L3" ~52 px fits the 158-px panel. */
    lbl_cpi  = make_label(p, 14, LAYER_Y + 50,
                          &lv_font_montserrat_14, COL_FG, "CPI ----");
    lbl_scrl = make_label(p, 86, LAYER_Y + 50,
                          &lv_font_montserrat_14, COL_FG, "SCRL --");

    /* Vertical divider between the layer label and the right status panel. */
    lv_obj_t *sep = lv_obj_create(p);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, LAYER_DIV_X, LAYER_Y + 8);
    lv_obj_set_size(sep, 1, LAYER_H - 16);
    lv_obj_set_style_bg_color(sep, COL_SEP, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    /* ---- Right panel: 24x24 OS icons on top (bigger than the previous
     * 16x16 versions so the OS-mode indicator reads at a glance), with
     * BLE + USB pills side-by-side below.
     *
     * Right panel spans LAYER_RIGHT_X..SCREEN_W (166..280, width 114).
     * Both groups are independently centered in that span:
     *   - OS icons: 24+12+24 = 60 px → start at 166+(114-60)/2 = 193
     *     (mac at 193+36 = 229). Left/right margins both 27 px.
     *   - BLE+USB:  ~"BLE 5" + 8 px + "USB" ≈ 82 px → start at 182
     *     (USB at 182+52 = 234), leaving roughly symmetric slack. */
    int x_os_win  = 193;
    int x_ble     = 182;

    img_os_win = lv_image_create(p);
    lv_obj_set_pos(img_os_win, x_os_win, LAYER_Y + 6);
    lv_image_set_src(img_os_win, &os_icon_windows);
    lv_obj_set_style_image_recolor(img_os_win, COL_INACTIVE, 0);
    lv_obj_set_style_image_recolor_opa(img_os_win, LV_OPA_COVER, 0);

    img_os_mac = lv_image_create(p);
    lv_obj_set_pos(img_os_mac, x_os_win + 36, LAYER_Y + 6);
    lv_image_set_src(img_os_mac, &os_icon_apple);
    lv_obj_set_style_image_recolor(img_os_mac, COL_INACTIVE, 0);
    lv_obj_set_style_image_recolor_opa(img_os_mac, LV_OPA_COVER, 0);

    /* BLE / USB horizontal row, beneath the OS icons. */
    lbl_ble_pill = make_label(p, x_ble, LAYER_Y + 50,
                              &lv_font_montserrat_14, COL_DIM, "BLE -");
    lbl_usb_pill = make_label(p, x_ble + 52, LAYER_Y + 50,
                              &lv_font_montserrat_14, COL_DIM, "USB");
}

/* Bar geometry shared by build + refresh. The H:/W: prefix is its own
 * label slotted between the source icon and the main bar, so label_pct
 * only carries the number ("23%" / "--%") — no more "H:" embedded.
 *
 * Layout, left-to-right, font_montserrat_16 except prefix (14):
 *   prefix at x=58           ("H:" / "W:", left-aligned, ~14 px)
 *   bar    at x=74,  w=86    (right edge 160)
 *   pct    at x=164, w=38    (right edge 202; "100%" worst case ~37 px)
 *   eta_a  at x=204, w=28    (right edge 232; "23h"  worst case ~28 px)
 *   eta_b  at x=236, w=40    (right edge 276; "59m"  worst case ~32 px,
 *                             4 px margin to screen so the trailing 'm'
 *                             never grazes the edge)
 * Splitting ETA into two right-aligned slots tabulates the unit suffixes:
 * 'h'/'d' always lands inside eta_a's right edge, 'm'/'h' inside eta_b's,
 * so rows line up vertically regardless of which format each row is in. */
#define RATE_LBL_PREFIX_X  58
#define RATE_BAR_X         74
#define RATE_BAR_W         86
#define RATE_LBL_PCT_X     164
#define RATE_LBL_PCT_W     38
#define RATE_LBL_ETA_A_X   204
#define RATE_LBL_ETA_A_W   28
#define RATE_LBL_ETA_B_X   236
#define RATE_LBL_ETA_B_W   40

static void build_rate_row(lv_obj_t *p, int src, int row, int y) {
    struct rate_row_widgets *w = &rate_rows[src][row];

    w->bar = lv_bar_create(p);
    lv_obj_set_pos(w->bar, RATE_BAR_X, y + 4);
    lv_obj_set_size(w->bar, RATE_BAR_W, 10);
    lv_bar_set_range(w->bar, 0, 100);
    lv_bar_set_value(w->bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(w->bar, COL_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w->bar, COL_DIM, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(w->bar, BAR_RATE_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_radius(w->bar, BAR_RATE_RADIUS, LV_PART_INDICATOR);

    /* Pace tick: a vertical line that slides along the bar at the
     * elapsed-ratio position. Slightly taller than the main bar (PACE_TICK_H
     * vs bar height 10) so it pokes above/below — easy to spot even when
     * the bar itself is fully colored. COL_FG (white) reads against every
     * pace-color the main bar takes (green/yellow/red). */
    w->bar_pace_tick = lv_obj_create(p);
    lv_obj_remove_style_all(w->bar_pace_tick);
    lv_obj_set_pos(w->bar_pace_tick, RATE_BAR_X, y + 4 - 2);
    lv_obj_set_size(w->bar_pace_tick, PACE_TICK_W, PACE_TICK_H);
    lv_obj_set_style_bg_color(w->bar_pace_tick, COL_FG, 0);
    lv_obj_set_style_bg_opa(w->bar_pace_tick, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w->bar_pace_tick, 1, 0);
    lv_obj_add_flag(w->bar_pace_tick, LV_OBJ_FLAG_HIDDEN);

    w->label_prefix = make_label(p, RATE_LBL_PREFIX_X, y + 2,
                                 &lv_font_montserrat_14, COL_DIM,
                                 row == 0 ? "H:" : "W:");

    /* Pct and ETA pieces are right-aligned within fixed-width slots so
     * the trailing digit / unit holds a stable column position as content
     * varies. eta_a / eta_b are two slots so 'h'/'d' and 'm'/'h' line up
     * tabularly across rows (see RATE_LBL_ETA_* comments above). */
    w->label_pct = make_rate_value_label(p, RATE_LBL_PCT_X, y + 2,
                                         RATE_LBL_PCT_W, "--%");
    w->label_eta_a = make_rate_value_label(p, RATE_LBL_ETA_A_X, y + 2,
                                           RATE_LBL_ETA_A_W, "");
    w->label_eta_b = make_rate_value_label(p, RATE_LBL_ETA_B_X, y + 2,
                                           RATE_LBL_ETA_B_W, "--");
}

/* Build the per-source icon spanning both rows of a source. The icon sits
 * at the left of the rate zone where the "Codex"/"Claude" text labels
 * used to be. Initial tint is COL_INACTIVE — refresh() promotes it to the
 * brand color once data arrives so the icon dims+brightens in lockstep
 * with the rest of the source's row (bars, labels). */
static void build_rate_icon(lv_obj_t *p, int src, int row0_y,
                            const lv_image_dsc_t *icon) {
    lv_obj_t *img = lv_image_create(p);
    /* Row spacing was tightened (22 → 28 px) to absorb the 32 px of dead
     * space that used to sit below the claude row. With the new spacing,
     * row 0 bar centers at row0_y+9 and row 1 bar at row0_y+37 — midpoint
     * row0_y+23. A 32 px icon centered on that midpoint lands its top at
     * row0_y+7. x=20 keeps the small left margin; the prefix label at
     * RATE_LBL_PREFIX_X=58 sits 6 px past the icon's right edge (52). */
    lv_obj_set_pos(img, 20, row0_y + 7);
    lv_image_set_src(img, icon);
    lv_obj_set_style_image_recolor(img, COL_INACTIVE, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    icon_src[src] = img;
}

static void build_rate(lv_obj_t *p) {
    /* Row offsets pulled wider (was 0/22/48/70 with 32 px of slack at the
     * bottom). New layout 0/28/60/88 distributes that slack evenly between
     * inter-row and inter-source gaps; last row's bar bottom now lands at
     * RATE_Y+102 = 226, leaving a clean 14 px breathing margin to the
     * screen's bottom edge at y=240. */
    /* Codex (rows at +0 and +28) */
    build_rate_row(p, METEORITE_RATE_CODEX, 0, RATE_Y + 0);
    build_rate_row(p, METEORITE_RATE_CODEX, 1, RATE_Y + 28);
    build_rate_icon(p, METEORITE_RATE_CODEX,  RATE_Y + 0,  &rate_icon_codex);
    /* Claude (rows at +60 and +88) */
    build_rate_row(p, METEORITE_RATE_CLAUDE, 0, RATE_Y + 60);
    build_rate_row(p, METEORITE_RATE_CLAUDE, 1, RATE_Y + 88);
    build_rate_icon(p, METEORITE_RATE_CLAUDE, RATE_Y + 60, &rate_icon_claude);
}

/* ========== Public API ========== */

void meteorite_layout_create(lv_obj_t *parent) {
    if (root) {
        LOG_WRN("meteorite_layout already created");
        return;
    }
    root = parent;

    lv_obj_set_style_bg_color(parent, COL_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    build_header(parent);
    build_layer(parent);
    build_rate(parent);

    LOG_INF("meteorite_layout created");
}

void meteorite_layout_destroy(void) {
    /* Widgets are children of `root` (the LVGL screen). The screen's clean
     * tear-down is the system's responsibility — we just drop our handles. */
    root             = NULL;
    lbl_kb_name      = NULL;
    lbl_rssi_dbm     = NULL;
    lbl_battery_pct  = NULL;
    lbl_layer_main   = NULL;
    lbl_cpi          = NULL;
    lbl_scrl         = NULL;
    img_os_win       = NULL;
    img_os_mac       = NULL;
    lbl_ble_pill     = NULL;
    lbl_usb_pill     = NULL;
    memset(signal_bars, 0, sizeof(signal_bars));
    memset(rate_rows,   0, sizeof(rate_rows));
    memset(icon_src,    0, sizeof(icon_src));
}

void meteorite_layout_refresh(void) {
    if (!root) return;

    struct meteorite_snapshot s;
    meteorite_data_snapshot(&s);

    char buf[32];
    uint32_t now_ms = k_uptime_get_32();
    bool layer_list_live    = s.has_layer_list;
    bool custom_config_live = s.has_custom_config;

    /* ===== Header ===== */
    if (lbl_kb_name) {
        lv_label_set_text(lbl_kb_name,
            s.has_keyboard ? s.keyboard_name : "Scanning...");
    }
    if (lbl_rssi_dbm) {
        uint8_t bars = s.has_keyboard ? rssi_to_bars(s.rssi_dbm) : 0;
        lv_color_t fill = bars >= 3 ? COL_GOOD
                        : bars >= 2 ? COL_WARN
                                    : COL_BAD;
        for (int i = 0; i < SIGNAL_BAR_COUNT; i++) {
            if (!signal_bars[i]) continue;
            lv_obj_set_style_bg_color(signal_bars[i],
                i < bars ? fill : COL_INACTIVE, 0);
        }
        if (s.has_keyboard) {
            snprintf(buf, sizeof(buf), "%d dBm", (int)s.rssi_dbm);
            lv_label_set_text(lbl_rssi_dbm, buf);
        } else {
            lv_label_set_text(lbl_rssi_dbm, "-- dBm");
        }
    }
    if (lbl_battery_pct) {
        if (s.has_keyboard && s.battery_central > 0) {
            snprintf(buf, sizeof(buf), "%u%%", (unsigned)s.battery_central);
            lv_label_set_text(lbl_battery_pct, buf);
            lv_obj_set_style_text_color(lbl_battery_pct,
                battery_color(s.battery_central), 0);
        } else {
            lv_label_set_text(lbl_battery_pct, "--%");
            lv_obj_set_style_text_color(lbl_battery_pct, COL_DIM, 0);
        }
    }

    /* ===== Layer ===== */
    if (lbl_layer_main) {
        char name[METEORITE_LAYER_NAME_LEN] = "----";
        if (s.has_keyboard) {
            /* Prefer the full layer-list name if available, otherwise the
             * 4-char dynamic name from the v1 ADV. */
            if (layer_list_live && s.active_layer < s.layer_count &&
                s.layer_names[s.active_layer][0] != '\0') {
                memcpy(name, s.layer_names[s.active_layer], sizeof(name));
            } else if (s.active_layer_name[0] != '\0') {
                memcpy(name, s.active_layer_name, sizeof(name));
            }
            snprintf(buf, sizeof(buf), "L%u %.*s",
                     (unsigned)s.active_layer,
                     (int)sizeof(name) - 1, name);
        } else {
            snprintf(buf, sizeof(buf), "L- ----");
        }
        lv_label_set_text(lbl_layer_main, buf);
    }

    /* ===== Layer right panel: OS + BLE + USB ===== */
    /* OS icons: both always present, the detected one bright (COL_ACCENT)
     * and the other dim (COL_INACTIVE). Same active/inactive pattern as
     * the BLE/USB pills below — visually consistent across the panel. */
    if (img_os_win && img_os_mac) {
        bool win_active = custom_config_live && (s.os_mode == METEORITE_OS_WIN);
        bool mac_active = custom_config_live && (s.os_mode == METEORITE_OS_MAC);
        lv_obj_set_style_image_recolor(img_os_win,
            win_active ? COL_ACCENT : COL_INACTIVE, 0);
        lv_obj_set_style_image_recolor(img_os_mac,
            mac_active ? COL_ACCENT : COL_INACTIVE, 0);
    }

    /* ===== Config ===== */
    if (lbl_cpi) {
        if (custom_config_live && s.cpi_value > 0) {
            snprintf(buf, sizeof(buf), "CPI %u", (unsigned)s.cpi_value);
            lv_label_set_text(lbl_cpi, buf);
            lv_obj_set_style_text_color(lbl_cpi, COL_FG, 0);
        } else {
            lv_label_set_text(lbl_cpi, "CPI ----");
            lv_obj_set_style_text_color(lbl_cpi, COL_DIM, 0);
        }
    }
    if (lbl_scrl) {
        if (custom_config_live && s.scroll_layer_1 != 0xFF) {
            snprintf(buf, sizeof(buf), "SCRL L%u",
                     (unsigned)s.scroll_layer_1);
            lv_label_set_text(lbl_scrl, buf);
            lv_obj_set_style_text_color(lbl_scrl, COL_FG, 0);
        } else {
            lv_label_set_text(lbl_scrl, "SCRL --");
            lv_obj_set_style_text_color(lbl_scrl, COL_DIM, 0);
        }
    }
    if (lbl_ble_pill) {
        if (s.has_keyboard && s.ble_connected) {
            snprintf(buf, sizeof(buf), "BLE %u",
                     (unsigned)s.ble_profile_slot);
            lv_label_set_text(lbl_ble_pill, buf);
            lv_obj_set_style_text_color(lbl_ble_pill, COL_ACCENT, 0);
        } else {
            lv_label_set_text(lbl_ble_pill, "BLE -");
            lv_obj_set_style_text_color(lbl_ble_pill, COL_DIM, 0);
        }
    }
    if (lbl_usb_pill) {
        lv_obj_set_style_text_color(lbl_usb_pill,
            (s.has_keyboard && s.usb_connected) ? COL_ACCENT : COL_DIM, 0);
    }

    /* ===== Rate limits ===== */
    for (int src = 0; src < METEORITE_RATE_COUNT; src++) {
        const struct meteorite_rate_limit *r = &s.rate[src];
        bool stale = !r->valid ||
                     (now_ms - r->captured_at_ms) > RATE_STALE_MS;

        /* Dim the source icon while stale so the user can tell at a
         * glance which sources are live without reading the bars. */
        if (icon_src[src]) {
            lv_obj_set_style_image_recolor(
                icon_src[src],
                stale ? COL_INACTIVE
                      : (src == METEORITE_RATE_CODEX ? COL_CODEX : COL_CLAUDE),
                0);
        }

        for (int row = 0; row < 2; row++) {
            struct rate_row_widgets *w = &rate_rows[src][row];
            if (!w->bar) continue;

            uint8_t pct = (row == 0) ? r->pct_5h : r->pct_weekly;
            uint32_t sec_at_capture = (row == 0) ? r->sec_until_5h
                                                 : r->sec_until_w;
            uint32_t total_window  = (row == 0) ? WINDOW_5H_SEC
                                                : WINDOW_W_SEC;
            uint32_t elapsed = r->valid
                ? (now_ms - r->captured_at_ms) / 1000u : 0;
            uint32_t remaining = (sec_at_capture > elapsed)
                ? sec_at_capture - elapsed : 0;

            /* Row 0 = 5-hour window, row 1 = weekly. The "H:" / "W:"
             * tag is its own label (label_prefix, set once at build),
             * slotted between the source icon and the main bar so the
             * row reads icon | H: | ████ | 23% | 1h23 left-to-right. */
            if (stale) {
                lv_bar_set_value(w->bar, 0, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(w->bar, COL_INACTIVE,
                                          LV_PART_INDICATOR);
                lv_obj_add_flag(w->bar_pace_tick, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(w->label_pct, "--%");
                lv_label_set_text(w->label_eta_a, "");
                lv_label_set_text(w->label_eta_b, "--");
                lv_obj_set_style_text_color(w->label_prefix, COL_DIM, 0);
                lv_obj_set_style_text_color(w->label_pct,    COL_DIM, 0);
                lv_obj_set_style_text_color(w->label_eta_a,  COL_DIM, 0);
                lv_obj_set_style_text_color(w->label_eta_b,  COL_DIM, 0);
            } else {
                uint8_t elapsed_pct =
                    elapsed_pct_from_remaining(remaining, total_window);
                lv_bar_set_value(w->bar, pct, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(w->bar,
                    pace_color(pct, elapsed_pct), LV_PART_INDICATOR);
                /* Slide the pace tick along the bar (x = bar_x + bar_w *
                 * elapsed_pct / 100), centered on its 2px width. */
                int tick_x = RATE_BAR_X
                           + (RATE_BAR_W * (int)elapsed_pct) / 100
                           - PACE_TICK_W / 2;
                lv_obj_set_x(w->bar_pace_tick, tick_x);
                lv_obj_clear_flag(w->bar_pace_tick, LV_OBJ_FLAG_HIDDEN);
                snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
                lv_label_set_text(w->label_pct, buf);
                char eta_a[8], eta_b[8];
                fmt_eta_parts(eta_a, sizeof(eta_a),
                              eta_b, sizeof(eta_b), remaining);
                lv_label_set_text(w->label_eta_a, eta_a);
                lv_label_set_text(w->label_eta_b, eta_b);
                lv_obj_set_style_text_color(w->label_prefix, COL_FG, 0);
                lv_obj_set_style_text_color(w->label_pct,    COL_FG, 0);
                /* Reset-time (ETA) was COL_DIM — bumped to COL_FG so a
                 * glance picks up when each window flips. */
                lv_obj_set_style_text_color(w->label_eta_a,  COL_FG, 0);
                lv_obj_set_style_text_color(w->label_eta_b,  COL_FG, 0);
            }
        }
    }
}
