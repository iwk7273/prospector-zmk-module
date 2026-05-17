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

#define LAYER_Y   36
#define LAYER_H   84

#define CFG_Y     120
#define CFG_H     28

#define RATE_Y    148
#define RATE_H    92

/* Layer zone is split into a left "big layer label" panel and a right
 * connectivity-status stack (OS icons + BLE + USB pills). LAYER_LEFT_W
 * is the layer label's reserved width; LAYER_RIGHT_X is where the right
 * panel starts. A 1-px COL_SEP divider sits between them. */
#define LAYER_LEFT_W   186
#define LAYER_DIV_X    (LAYER_LEFT_W + 2)
#define LAYER_RIGHT_X  (LAYER_DIV_X + 6)

/* Bar corner radii. Main rate bar is 10px tall → radius 5 = pill ends.
 * Pace bar is 2px tall → radius 1 still rounds visibly. RSSI bar is 8px
 * → radius 4 = pill. */
#define BAR_RATE_RADIUS   5
#define BAR_PACE_RADIUS   1
#define BAR_RSSI_RADIUS   4

/* v2 ext-adv fields go stale 30s after the last received packet, even if
 * the v1 stream is still flowing. v2 cadence is ~1Hz so this is generous. */
#define V2_STALE_MS  30000u

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
static lv_obj_t *bar_rssi         = NULL;
static lv_obj_t *lbl_rssi_dbm     = NULL;
static lv_obj_t *lbl_battery_pct  = NULL;

/* Layer zone */
static lv_obj_t *lbl_layer_main   = NULL;  /* "L0 BASE" — sublayer list removed per UX request */

/* Config zone */
static lv_obj_t *lbl_cpi          = NULL;
static lv_obj_t *lbl_scrl         = NULL;

/* Layer-zone right panel — connectivity + OS-mode status. Both OS icons
 * are always laid out; refresh() tints the detected one COL_ACCENT and
 * the other COL_INACTIVE, mirroring the BLE/USB pill active/dim pattern. */
static lv_obj_t *img_os_win       = NULL;
static lv_obj_t *img_os_mac       = NULL;
static lv_obj_t *lbl_ble_pill     = NULL;
static lv_obj_t *lbl_usb_pill     = NULL;

/* ====== OS icons — 16×16 1bpp bitmaps approximating Phosphor glyphs ======
 * LVGL v9 renders LV_COLOR_FORMAT_A1 through the widget's image_recolor,
 * so the icons inherit the text color set on the lv_image widget. Bit
 * order is MSB-first within each byte; row stride is 2 bytes (16 px / 8). */

static const uint8_t os_icon_windows_map[] = {
    0x00, 0x00,
    0x00, 0x00,
    0x3F, 0x3F,  /* ..######..######  top row of top-left + top-right squares */
    0x3F, 0x3F,
    0x3F, 0x3F,
    0x3F, 0x3F,
    0x3F, 0x3F,
    0x3F, 0x3F,
    0x00, 0x00,  /* vertical gap between top and bottom rows */
    0x00, 0x00,
    0x3F, 0x3F,  /* bottom-left + bottom-right squares */
    0x3F, 0x3F,
    0x3F, 0x3F,
    0x3F, 0x3F,
    0x3F, 0x3F,
    0x3F, 0x3F,
};

static const lv_image_dsc_t os_icon_windows = {
    .header = {
        .magic  = LV_IMAGE_HEADER_MAGIC,
        .cf     = LV_COLOR_FORMAT_A1,
        .flags  = 0,
        .w      = 16,
        .h      = 16,
        .stride = 2,
    },
    .data_size = sizeof(os_icon_windows_map),
    .data = os_icon_windows_map,
};

static const uint8_t os_icon_apple_map[] = {
    0x01, 0x00,  /* .......#........  leaf top */
    0x03, 0x00,  /* ......##........  */
    0x03, 0x00,  /* ......##........  */
    0x1F, 0xE0,  /* ...########.....  apple body top */
    0x3F, 0xF0,  /* ..##########....  */
    0x7F, 0xF8,  /* .############... */
    0x7F, 0xF8,
    0x7F, 0xF8,
    0x7F, 0xF8,
    0x7F, 0xF8,
    0x7F, 0xF8,
    0x3F, 0xF0,
    0x3F, 0xF0,
    0x1F, 0xE0,
    0x0F, 0xC0,  /* ....######...... */
    0x07, 0x80,  /* .....####....... */
};

static const lv_image_dsc_t os_icon_apple = {
    .header = {
        .magic  = LV_IMAGE_HEADER_MAGIC,
        .cf     = LV_COLOR_FORMAT_A1,
        .flags  = 0,
        .w      = 16,
        .h      = 16,
        .stride = 2,
    },
    .data_size = sizeof(os_icon_apple_map),
    .data = os_icon_apple_map,
};

/* ====== Rate-source icons — 24x24 1bpp, generated by _gen_rate_icons.py.
 * Spans both window rows (5h + weekly) at the left of the rate zone, so
 * one icon = one source. Rendered via image_recolor like the OS icons. */

static const uint8_t rate_icon_claude_map[] = {
    0x00, 0x1C, 0x00,  /*            ###           */
    0x00, 0x1C, 0x00,  /*            ###           */
    0x00, 0x1C, 0x00,  /*            ###           */
    0x00, 0x1C, 0x00,  /*            ###           */
    0x20, 0x1C, 0x02,  /*   #        ###        #  */
    0x18, 0x1C, 0x0C,  /*    ##      ###      ##   */
    0x0C, 0x1C, 0x18,  /*     ##     ###     ##    */
    0x06, 0x1C, 0x30,  /*      ##    ###    ##     */
    0x03, 0x1C, 0x60,  /*       ##   ###   ##      */
    0x01, 0x9C, 0xC0,  /*        ##  ###  ##       */
    0x00, 0xDD, 0x80,  /*         ## ### ##        */
    0xFF, 0xFF, 0xFF,  /* ######################## */
    0xFF, 0xFF, 0xFF,  /* ######################## */
    0x00, 0xDD, 0x80,  /*         ## ### ##        */
    0x01, 0x9C, 0xC0,  /*        ##  ###  ##       */
    0x03, 0x1C, 0x60,  /*       ##   ###   ##      */
    0x06, 0x1C, 0x30,  /*      ##    ###    ##     */
    0x0C, 0x1C, 0x18,  /*     ##     ###     ##    */
    0x18, 0x1C, 0x0C,  /*    ##      ###      ##   */
    0x20, 0x1C, 0x02,  /*   #        ###        #  */
    0x00, 0x1C, 0x00,  /*            ###           */
    0x00, 0x1C, 0x00,  /*            ###           */
    0x00, 0x1C, 0x00,  /*            ###           */
    0x00, 0x1C, 0x00,  /*            ###           */
};

static const lv_image_dsc_t rate_icon_claude = {
    .header = {
        .magic  = LV_IMAGE_HEADER_MAGIC,
        .cf     = LV_COLOR_FORMAT_A1,
        .flags  = 0,
        .w      = 24,
        .h      = 24,
        .stride = 3,
    },
    .data_size = sizeof(rate_icon_claude_map),
    .data = rate_icon_claude_map,
};

static const uint8_t rate_icon_codex_map[] = {
    0x00, 0xFF, 0x00,  /*         ########         */
    0x03, 0xC3, 0xC0,  /*       ####    ####       */
    0x0E, 0x00, 0x70,  /*     ###          ###     */
    0x18, 0x00, 0x18,  /*    ##              ##    */
    0x30, 0x00, 0x0C,  /*   ##                ##   */
    0x60, 0x00, 0x06,  /*  ##                  ##  */
    0xC0, 0x00, 0x03,  /* ##                    ## */
    0xC0, 0x00, 0x03,  /* ##                    ## */
    0xC0, 0x7E, 0x03,  /* ##       ######       ## */
    0xC0, 0xC3, 0x03,  /* ##      ##    ##      ## */
    0xC1, 0x81, 0x83,  /* ##     ##      ##     ## */
    0xC1, 0x81, 0x83,  /* ##     ##      ##     ## */
    0xC1, 0x81, 0x83,  /* ##     ##      ##     ## */
    0xC1, 0x81, 0x83,  /* ##     ##      ##     ## */
    0xC0, 0xC3, 0x03,  /* ##      ##    ##      ## */
    0xC0, 0x7E, 0x03,  /* ##       ######       ## */
    0xC0, 0x00, 0x03,  /* ##                    ## */
    0xC0, 0x00, 0x03,  /* ##                    ## */
    0x60, 0x00, 0x06,  /*  ##                  ##  */
    0x30, 0x00, 0x0C,  /*   ##                ##   */
    0x18, 0x00, 0x18,  /*    ##              ##    */
    0x0E, 0x00, 0x70,  /*     ###          ###     */
    0x03, 0xC3, 0xC0,  /*       ####    ####       */
    0x00, 0xFF, 0x00,  /*         ########         */
};

static const lv_image_dsc_t rate_icon_codex = {
    .header = {
        .magic  = LV_IMAGE_HEADER_MAGIC,
        .cf     = LV_COLOR_FORMAT_A1,
        .flags  = 0,
        .w      = 24,
        .h      = 24,
        .stride = 3,
    },
    .data_size = sizeof(rate_icon_codex_map),
    .data = rate_icon_codex_map,
};

/* Rate zone (2 sources × 2 rows). The source label is now a 24x24 icon
 * spanning both rows (icon_src[src]) — no per-row src label anymore. */
struct rate_row_widgets {
    lv_obj_t *bar_pace;      /* thin elapsed-ratio bar above the main bar */
    lv_obj_t *bar;
    lv_obj_t *label_pct;
    lv_obj_t *label_eta;
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

/* Format remaining seconds into "Hh:Mm" (<24h) or "Dd Hh" (>=24h) */
static void fmt_eta(char *out, size_t sz, uint32_t remaining_s) {
    if (remaining_s == 0) {
        snprintf(out, sz, "--:--");
        return;
    }
    if (remaining_s >= 86400u) {
        uint32_t d = remaining_s / 86400u;
        uint32_t h = (remaining_s % 86400u) / 3600u;
        snprintf(out, sz, "%ud %uh", (unsigned)d, (unsigned)h);
    } else {
        uint32_t h = remaining_s / 3600u;
        uint32_t m = (remaining_s % 3600u) / 60u;
        snprintf(out, sz, "%uh:%02um", (unsigned)h, (unsigned)m);
    }
}

/* ========== Builders ========== */

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y,
                            const lv_font_t *font, lv_color_t color,
                            const char *initial) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
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

    /* dBm number sits left of the bar, right-aligned so trailing digits
     * stay flush. Widened to 60px to fit "-127 dBm" cleanly, and brightened
     * to COL_FG (was COL_DIM) so the actual RSSI reading is legible at a
     * glance rather than being secondary to the bar. */
    lbl_rssi_dbm = lv_label_create(p);
    lv_obj_set_pos(lbl_rssi_dbm, 120, HDR_Y + 11);
    lv_obj_set_size(lbl_rssi_dbm, 60, 16);
    lv_obj_set_style_text_font(lbl_rssi_dbm, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_rssi_dbm, COL_FG, 0);
    lv_obj_set_style_text_align(lbl_rssi_dbm, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_rssi_dbm, "-- dBm");

    bar_rssi = lv_bar_create(p);
    lv_obj_set_pos(bar_rssi, 184, HDR_Y + 14);
    lv_obj_set_size(bar_rssi, 34, 8);
    lv_bar_set_range(bar_rssi, 0, 5);
    lv_bar_set_value(bar_rssi, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_rssi, COL_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_rssi, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_rssi, COL_FG, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_rssi, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_rssi, BAR_RSSI_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_rssi, BAR_RSSI_RADIUS, LV_PART_INDICATOR);

    lbl_battery_pct = make_label(p, 232, HDR_Y + 10,
                                 &lv_font_montserrat_16, COL_FG, "--%");
}

static void build_layer(lv_obj_t *p) {
    /* Big "L0 BASE" label — left-aligned in the layer zone's left 2/3
     * (LAYER_LEFT_W reserved). Vertically centered: LAYER_H=84, font
     * height ~36, so a +24 offset from LAYER_Y gives reasonable centering. */
    lbl_layer_main = lv_label_create(p);
    lv_obj_set_style_text_font(lbl_layer_main, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_layer_main, COL_FG, 0);
    lv_label_set_text(lbl_layer_main, "L- ----");
    lv_obj_set_pos(lbl_layer_main, 16, LAYER_Y + 24);

    /* Vertical divider between the layer label and the right status stack.
     * Built as an anonymous strip (no handle kept) — it has no state and
     * tears down with the parent screen. */
    lv_obj_t *sep = lv_obj_create(p);
    lv_obj_remove_style_all(sep);
    lv_obj_set_pos(sep, LAYER_DIV_X, LAYER_Y + 10);
    lv_obj_set_size(sep, 1, LAYER_H - 20);
    lv_obj_set_style_bg_color(sep, COL_SEP, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    /* ---- Right panel: OS icons (row 1), BLE pill (row 2), USB pill (row 3).
     * All left-aligned at x_left for a tidy column; rows spaced ~22px. */
    int x_left = LAYER_RIGHT_X + 4;

    img_os_win = lv_image_create(p);
    lv_obj_set_pos(img_os_win, x_left, LAYER_Y + 12);
    lv_image_set_src(img_os_win, &os_icon_windows);
    lv_obj_set_style_image_recolor(img_os_win, COL_INACTIVE, 0);
    lv_obj_set_style_image_recolor_opa(img_os_win, LV_OPA_COVER, 0);

    img_os_mac = lv_image_create(p);
    lv_obj_set_pos(img_os_mac, x_left + 26, LAYER_Y + 12);
    lv_image_set_src(img_os_mac, &os_icon_apple);
    lv_obj_set_style_image_recolor(img_os_mac, COL_INACTIVE, 0);
    lv_obj_set_style_image_recolor_opa(img_os_mac, LV_OPA_COVER, 0);

    lbl_ble_pill = make_label(p, x_left, LAYER_Y + 36,
                              &lv_font_montserrat_14, COL_DIM, "BLE -");
    lbl_usb_pill = make_label(p, x_left, LAYER_Y + 58,
                              &lv_font_montserrat_14, COL_DIM, "USB");
}

static void build_config(lv_obj_t *p) {
    /* OS / BLE / USB moved up to the layer zone's right panel, so this
     * strip is just CPI + SCRL now. Distribute across the full width with
     * comfortable margins instead of cramming them on the left. */
    int y_text = CFG_Y + 6;
    lbl_cpi  = make_label(p,  12, y_text, &lv_font_montserrat_14, COL_FG,
                          "CPI ----");
    lbl_scrl = make_label(p, 152, y_text, &lv_font_montserrat_14, COL_FG,
                          "SCRL --");
}

static void build_rate_row(lv_obj_t *p, int src, int row, int y) {
    struct rate_row_widgets *w = &rate_rows[src][row];

    /* Pace bar (elapsed-ratio of the window): 2px above the main bar.
     * Subtle dim indicator so it reads as a tick mark, not a competing
     * usage bar. The main bar's pace-coded color is the primary signal. */
    w->bar_pace = lv_bar_create(p);
    lv_obj_set_pos(w->bar_pace, 60, y + 0);
    lv_obj_set_size(w->bar_pace, 110, 2);
    lv_bar_set_range(w->bar_pace, 0, 100);
    lv_bar_set_value(w->bar_pace, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(w->bar_pace, COL_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w->bar_pace, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w->bar_pace, COL_DIM, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->bar_pace, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(w->bar_pace, BAR_PACE_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_radius(w->bar_pace, BAR_PACE_RADIUS, LV_PART_INDICATOR);

    w->bar = lv_bar_create(p);
    lv_obj_set_pos(w->bar, 60, y + 4);
    lv_obj_set_size(w->bar, 110, 10);
    lv_bar_set_range(w->bar, 0, 100);
    lv_bar_set_value(w->bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(w->bar, COL_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w->bar, COL_DIM, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(w->bar, BAR_RATE_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_radius(w->bar, BAR_RATE_RADIUS, LV_PART_INDICATOR);

    w->label_pct = make_label(p, 176, y + 2,
                              &lv_font_montserrat_14, COL_DIM, "--%");
    w->label_eta = make_label(p, 218, y + 2,
                              &lv_font_montserrat_14, COL_DIM, "--:--");
}

/* Build the per-source icon spanning both rows of a source. The icon sits
 * at the left of the rate zone where the "Codex"/"Claude" text labels
 * used to be. Initial tint is COL_INACTIVE — refresh() promotes it to the
 * brand color once data arrives so the icon dims+brightens in lockstep
 * with the rest of the source's row (bars, labels). */
static void build_rate_icon(lv_obj_t *p, int src, int row0_y,
                            const lv_image_dsc_t *icon) {
    lv_obj_t *img = lv_image_create(p);
    /* Row 0 bars occupy row0_y .. row0_y+14 and row 1 occupies row0_y+22 ..
     * row0_y+36. Mid-point ≈ row0_y+18; a 24px icon centered on that lands
     * at row0_y+6. */
    lv_obj_set_pos(img, 6, row0_y + 6);
    lv_image_set_src(img, icon);
    lv_obj_set_style_image_recolor(img, COL_INACTIVE, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    icon_src[src] = img;
}

static void build_rate(lv_obj_t *p) {
    /* Codex (rows at +0 and +22) */
    build_rate_row(p, METEORITE_RATE_CODEX, 0, RATE_Y + 0);
    build_rate_row(p, METEORITE_RATE_CODEX, 1, RATE_Y + 22);
    build_rate_icon(p, METEORITE_RATE_CODEX,  RATE_Y + 0,  &rate_icon_codex);
    /* Claude (rows at +48 and +70) */
    build_rate_row(p, METEORITE_RATE_CLAUDE, 0, RATE_Y + 48);
    build_rate_row(p, METEORITE_RATE_CLAUDE, 1, RATE_Y + 70);
    build_rate_icon(p, METEORITE_RATE_CLAUDE, RATE_Y + 48, &rate_icon_claude);
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
    build_config(parent);
    build_rate(parent);

    LOG_INF("meteorite_layout created");
}

void meteorite_layout_destroy(void) {
    /* Widgets are children of `root` (the LVGL screen). The screen's clean
     * tear-down is the system's responsibility — we just drop our handles. */
    root             = NULL;
    lbl_kb_name      = NULL;
    bar_rssi         = NULL;
    lbl_rssi_dbm     = NULL;
    lbl_battery_pct  = NULL;
    lbl_layer_main   = NULL;
    lbl_cpi          = NULL;
    lbl_scrl         = NULL;
    img_os_win       = NULL;
    img_os_mac       = NULL;
    lbl_ble_pill     = NULL;
    lbl_usb_pill     = NULL;
    memset(rate_rows, 0, sizeof(rate_rows));
    memset(icon_src,  0, sizeof(icon_src));
}

void meteorite_layout_refresh(void) {
    if (!root) return;

    struct meteorite_snapshot s;
    meteorite_data_snapshot(&s);

    char buf[32];
    uint32_t now_ms = k_uptime_get_32();
    bool v2_stale = (s.v2_updated_at_ms == 0) ||
                    ((now_ms - s.v2_updated_at_ms) > V2_STALE_MS);
    bool layer_list_live   = s.has_layer_list   && !v2_stale;
    bool custom_config_live = s.has_custom_config && !v2_stale;

    /* ===== Header ===== */
    if (lbl_kb_name) {
        lv_label_set_text(lbl_kb_name,
            s.has_keyboard ? s.keyboard_name : "Scanning...");
    }
    if (bar_rssi && lbl_rssi_dbm) {
        if (s.has_keyboard) {
            uint8_t bars = rssi_to_bars(s.rssi_dbm);
            lv_bar_set_value(bar_rssi, bars, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(bar_rssi,
                bars >= 3 ? COL_GOOD : bars >= 2 ? COL_WARN : COL_BAD,
                LV_PART_INDICATOR);
            snprintf(buf, sizeof(buf), "%d dBm", (int)s.rssi_dbm);
            lv_label_set_text(lbl_rssi_dbm, buf);
        } else {
            lv_bar_set_value(bar_rssi, 0, LV_ANIM_OFF);
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

            if (stale) {
                lv_bar_set_value(w->bar, 0, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(w->bar, COL_INACTIVE,
                                          LV_PART_INDICATOR);
                lv_bar_set_value(w->bar_pace, 0, LV_ANIM_OFF);
                lv_label_set_text(w->label_pct, "--%");
                lv_label_set_text(w->label_eta, "--:--");
                lv_obj_set_style_text_color(w->label_pct, COL_DIM, 0);
                lv_obj_set_style_text_color(w->label_eta, COL_DIM, 0);
            } else {
                uint8_t elapsed_pct =
                    elapsed_pct_from_remaining(remaining, total_window);
                lv_bar_set_value(w->bar, pct, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(w->bar,
                    pace_color(pct, elapsed_pct), LV_PART_INDICATOR);
                lv_bar_set_value(w->bar_pace, elapsed_pct, LV_ANIM_OFF);
                snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
                lv_label_set_text(w->label_pct, buf);
                fmt_eta(buf, sizeof(buf), remaining);
                lv_label_set_text(w->label_eta, buf);
                lv_obj_set_style_text_color(w->label_pct, COL_FG, 0);
                lv_obj_set_style_text_color(w->label_eta, COL_DIM, 0);
            }
        }
    }
}
