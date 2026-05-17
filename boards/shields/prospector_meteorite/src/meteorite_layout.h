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
 *   Header   (y=0..36)     keyboard name, RSSI bar+dBm, battery %
 *   Layer    (y=36..120)   big active layer "Ln NAME", sub layer list
 *   Config   (y=120..148)  OS mode | CPI | SCRL | BLE pill | USB pill
 *   Rate     (y=148..240)  Codex / Claude 5h+weekly bars and reset time
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
