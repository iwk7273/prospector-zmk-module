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

/* ========== Widget handles ========== */

static lv_obj_t *root             = NULL;

/* Header */
static lv_obj_t *lbl_kb_name      = NULL;
static lv_obj_t *bar_rssi         = NULL;
static lv_obj_t *lbl_rssi_dbm     = NULL;
static lv_obj_t *lbl_battery_pct  = NULL;

/* Layer zone */
static lv_obj_t *lbl_layer_main   = NULL;  /* "L0 BASE" */
static lv_obj_t *lbl_layer_sub    = NULL;  /* "L2 NUM   L4 CFG" */

/* Config zone */
static lv_obj_t *lbl_os           = NULL;
static lv_obj_t *lbl_cpi          = NULL;
static lv_obj_t *lbl_scrl         = NULL;
static lv_obj_t *lbl_ble_pill     = NULL;
static lv_obj_t *lbl_usb_pill     = NULL;

/* Rate zone (2 sources × 2 rows) */
struct rate_row_widgets {
    lv_obj_t *label_src;     /* "Codex" / "Claude" only on first row */
    lv_obj_t *bar;
    lv_obj_t *label_pct;
    lv_obj_t *label_eta;
};
static struct rate_row_widgets rate_rows[METEORITE_RATE_COUNT][2];

/* ========== Helpers ========== */

static uint8_t rssi_to_bars(int8_t r) {
    if (r >= -50) return 5;
    if (r >= -60) return 4;
    if (r >= -70) return 3;
    if (r >= -80) return 2;
    if (r >= -90) return 1;
    return 0;
}

static lv_color_t pct_color(uint8_t pct) {
    if (pct >= 85) return COL_BAD;
    if (pct >= 60) return COL_WARN;
    return COL_GOOD;
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

static const char *os_mode_text(uint8_t os_mode) {
    switch (os_mode) {
    case METEORITE_OS_WIN: return "OS WIN";
    case METEORITE_OS_MAC: return "OS MAC";
    default:               return "OS --";
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

    lbl_kb_name = make_label(p, 6, HDR_Y + 10,
                             &lv_font_montserrat_16, COL_FG, "----");

    bar_rssi = lv_bar_create(p);
    lv_obj_set_pos(bar_rssi, 168, HDR_Y + 11);
    lv_obj_set_size(bar_rssi, 50, 8);
    lv_bar_set_range(bar_rssi, 0, 5);
    lv_bar_set_value(bar_rssi, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_rssi, COL_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_rssi, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_rssi, COL_FG, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_rssi, LV_OPA_COVER, LV_PART_INDICATOR);

    lbl_rssi_dbm = make_label(p, 168, HDR_Y + 20,
                              &lv_font_montserrat_12, COL_DIM, "-- dBm");

    lbl_battery_pct = make_label(p, 232, HDR_Y + 10,
                                 &lv_font_montserrat_16, COL_FG, "--%");
}

static void build_layer(lv_obj_t *p) {
    /* Big "L0 BASE" line, centered horizontally */
    lbl_layer_main = lv_label_create(p);
    lv_obj_set_style_text_font(lbl_layer_main, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_layer_main, COL_FG, 0);
    lv_label_set_text(lbl_layer_main, "L- ----");
    lv_obj_align(lbl_layer_main, LV_ALIGN_TOP_MID, 0, LAYER_Y + 14);

    /* Separator */
    lv_obj_t *sep = lv_obj_create(p);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 200, 1);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, LAYER_Y + 56);
    lv_obj_set_style_bg_color(sep, COL_SEP, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    /* Sub-layer list */
    lbl_layer_sub = lv_label_create(p);
    lv_obj_set_style_text_font(lbl_layer_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_layer_sub, COL_DIM, 0);
    lv_label_set_text(lbl_layer_sub, "");
    lv_obj_align(lbl_layer_sub, LV_ALIGN_TOP_MID, 0, LAYER_Y + 62);
}

static void build_config(lv_obj_t *p) {
    int y = CFG_Y + 6;
    lbl_os       = make_label(p, 8,   y, &lv_font_montserrat_14, COL_FG,  "OS --");
    lbl_cpi      = make_label(p, 68,  y, &lv_font_montserrat_14, COL_FG,  "CPI ----");
    lbl_scrl     = make_label(p, 140, y, &lv_font_montserrat_14, COL_FG,  "SCRL --");
    lbl_ble_pill = make_label(p, 208, y, &lv_font_montserrat_14, COL_DIM, "BLE -");
    lbl_usb_pill = make_label(p, 246, y, &lv_font_montserrat_14, COL_DIM, "USB");
}

static void build_rate_row(lv_obj_t *p, int src, int row, int y,
                           const char *src_label) {
    struct rate_row_widgets *w = &rate_rows[src][row];

    if (src_label) {
        w->label_src = make_label(p, 8, y + 2,
                                  &lv_font_montserrat_14, COL_FG, src_label);
    } else {
        w->label_src = NULL;
    }

    w->bar = lv_bar_create(p);
    lv_obj_set_pos(w->bar, 60, y + 4);
    lv_obj_set_size(w->bar, 110, 10);
    lv_bar_set_range(w->bar, 0, 100);
    lv_bar_set_value(w->bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(w->bar, COL_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w->bar, COL_DIM, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, LV_PART_INDICATOR);

    w->label_pct = make_label(p, 176, y + 2,
                              &lv_font_montserrat_14, COL_DIM, "--%");
    w->label_eta = make_label(p, 218, y + 2,
                              &lv_font_montserrat_14, COL_DIM, "--:--");
}

static void build_rate(lv_obj_t *p) {
    /* Codex */
    build_rate_row(p, METEORITE_RATE_CODEX,  0, RATE_Y + 0,  "Codex");
    build_rate_row(p, METEORITE_RATE_CODEX,  1, RATE_Y + 22, NULL);
    /* Claude */
    build_rate_row(p, METEORITE_RATE_CLAUDE, 0, RATE_Y + 48, "Claude");
    build_rate_row(p, METEORITE_RATE_CLAUDE, 1, RATE_Y + 70, NULL);
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
    lbl_layer_sub    = NULL;
    lbl_os           = NULL;
    lbl_cpi          = NULL;
    lbl_scrl         = NULL;
    lbl_ble_pill     = NULL;
    lbl_usb_pill     = NULL;
    memset(rate_rows, 0, sizeof(rate_rows));
}

void meteorite_layout_refresh(void) {
    if (!root) return;

    struct meteorite_snapshot s;
    meteorite_data_snapshot(&s);

    char buf[32];

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
            if (s.has_layer_list && s.active_layer < s.layer_count &&
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
    if (lbl_layer_sub) {
        /* List up to 4 non-active layers in a single line. */
        char sub[80] = "";
        size_t off = 0;
        if (s.has_layer_list) {
            int shown = 0;
            for (uint8_t i = 0; i < s.layer_count && shown < 4; i++) {
                if (i == s.active_layer) continue;
                if (s.layer_names[i][0] == '\0') continue;
                int n = snprintf(sub + off, sizeof(sub) - off,
                                 "%sL%u %.*s",
                                 shown == 0 ? "" : "   ",
                                 (unsigned)i,
                                 METEORITE_LAYER_NAME_LEN - 1,
                                 s.layer_names[i]);
                if (n < 0 || (size_t)n >= sizeof(sub) - off) break;
                off += n;
                shown++;
            }
        }
        lv_label_set_text(lbl_layer_sub, sub);
    }

    /* ===== Config ===== */
    if (lbl_os) {
        lv_label_set_text(lbl_os,
            s.has_custom_config ? os_mode_text(s.os_mode) : "OS --");
        lv_obj_set_style_text_color(lbl_os,
            s.has_custom_config ? COL_FG : COL_DIM, 0);
    }
    if (lbl_cpi) {
        if (s.has_custom_config && s.cpi_value > 0) {
            snprintf(buf, sizeof(buf), "CPI %u", (unsigned)s.cpi_value);
            lv_label_set_text(lbl_cpi, buf);
            lv_obj_set_style_text_color(lbl_cpi, COL_FG, 0);
        } else {
            lv_label_set_text(lbl_cpi, "CPI ----");
            lv_obj_set_style_text_color(lbl_cpi, COL_DIM, 0);
        }
    }
    if (lbl_scrl) {
        if (s.has_custom_config && s.scroll_layer_1 != 0xFF) {
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
    uint32_t now_ms = k_uptime_get_32();
    for (int src = 0; src < METEORITE_RATE_COUNT; src++) {
        const struct meteorite_rate_limit *r = &s.rate[src];
        bool stale = !r->valid ||
                     (now_ms - r->captured_at_ms) > 30u * 1000u;

        for (int row = 0; row < 2; row++) {
            struct rate_row_widgets *w = &rate_rows[src][row];
            if (!w->bar) continue;

            uint8_t pct = (row == 0) ? r->pct_5h : r->pct_weekly;
            uint32_t sec_at_capture = (row == 0) ? r->sec_until_5h
                                                 : r->sec_until_w;
            uint32_t elapsed = r->valid
                ? (now_ms - r->captured_at_ms) / 1000u : 0;
            uint32_t remaining = (sec_at_capture > elapsed)
                ? sec_at_capture - elapsed : 0;

            if (stale) {
                lv_bar_set_value(w->bar, 0, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(w->bar, COL_INACTIVE,
                                          LV_PART_INDICATOR);
                lv_label_set_text(w->label_pct, "--%");
                lv_label_set_text(w->label_eta, "--:--");
                lv_obj_set_style_text_color(w->label_pct, COL_DIM, 0);
                lv_obj_set_style_text_color(w->label_eta, COL_DIM, 0);
            } else {
                lv_bar_set_value(w->bar, pct, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(w->bar, pct_color(pct),
                                          LV_PART_INDICATOR);
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
