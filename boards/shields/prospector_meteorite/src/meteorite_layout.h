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
 *   Header   (y=0..36)     keyboard name, RSSI dBm + phone-style signal
 *                          bars, battery %
 *   Layer    (y=36..148)   left panel: big "Ln NAME" + CPI/SCRL sub-row
 *                          right panel: Win/Mac OS icons + BLE/USB pills
 *                          (1-px COL_SEP divider between panels)
 *   Rate     (y=148..240)  Codex / Claude 5h+weekly bars + pace tick
 *                          + ETA, source-brand 24x24 icons on the left
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
