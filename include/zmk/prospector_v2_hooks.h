/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * v2 advertisement hook API — weak symbol contract.
 *
 * The v2 packet carries keyboard-specific metadata (OS mode, trackball
 * CPI, scroll-layer indices) that prospector-zmk-module itself does
 * not own. Each getter below has a weak default that returns an
 * "unknown" sentinel, so prospector-zmk-module stays buildable in
 * isolation.
 *
 * Keyboards that want to populate these fields override the symbols
 * from their own module (e.g. zmk-feature-meteorite-config provides
 * a thin adapter that calls into its own state APIs).
 *
 * Layer names are NOT exposed via this hook — they come straight from
 * the ZMK keymap API (zmk_keymap_layer_name) inside the v2 builder.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the current OS mode for the keyboard.
 *   0    = Windows
 *   1    = macOS
 *   0xFF = unknown / not configured (weak default)
 */
uint8_t prospector_v2_get_os_mode(void);

/**
 * Returns the resolved trackball CPI value (e.g. 800).
 *   0 = unknown / not configured (weak default)
 */
uint16_t prospector_v2_get_cpi(void);

/**
 * Returns the layer index used for the primary scroll layer.
 *   0xFF = disabled / not configured (weak default)
 */
uint8_t prospector_v2_get_scroll_layer_1(void);

/**
 * Returns the layer index used for the secondary scroll layer.
 *   0xFF = disabled / not configured (weak default)
 */
uint8_t prospector_v2_get_scroll_layer_2(void);

/**
 * Returns the resolved scroll divisor value (e.g. 40).
 *   0 = unknown / not configured (weak default)
 */
uint16_t prospector_v2_get_scroll_div(void);

#ifdef __cplusplus
}
#endif
