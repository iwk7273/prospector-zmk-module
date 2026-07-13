/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Meteorite Layout — single-purpose status display for the Meteorite40
 * keyboard.
 *
 * Screen geometry: 280 (W) x 240 (H), ST7789V mounted in landscape.
 *
 * Zones:
 *   Header   (y=0..36)     keyboard name (left), RSSI dBm + 5-bar
 *                          phone-style signal icon, battery % (right,
 *                          right-aligned against the 268-px right margin)
 *   Layer    (y=36..124)   left panel: "Ln NAME" (font 28) + CPI/SCRL
 *                          right panel: 24x24 Win/Mac OS icons +
 *                          BLE/USB pills, both centered in the right
 *                          panel (1-px COL_SEP divider)
 *   Rate     (y=124..240)  Codex / Claude 5h+weekly bars + pace tick
 *                          + H:/W: prefixed % + ETA (font 16,
 *                          right-aligned); 32x32 source icons
 */

#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build the layout onto `parent` (typically the active LVGL screen). */
void meteorite_layout_create(lv_obj_t *parent);

/* Pull the latest meteorite_data snapshot and refresh widgets. Safe to call
 * from the LVGL timer / dedicated display thread. */
void meteorite_layout_refresh(void);

/* Tear down all widgets. Idempotent. */
void meteorite_layout_destroy(void);

#ifdef __cplusplus
}
#endif
