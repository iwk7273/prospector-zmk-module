/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Weak default implementations of the v2 ADV hook API.
 * Override these from keyboard-specific modules (e.g. zmk-feature-
 * meteorite-config) by defining the same symbols without __weak.
 */

#include <zmk/prospector_v2_hooks.h>

__weak uint8_t prospector_v2_get_os_mode(void)         { return 0xFF; }
__weak uint16_t prospector_v2_get_cpi(void)            { return 0; }
__weak uint8_t prospector_v2_get_scroll_layer_1(void)  { return 0xFF; }
__weak uint8_t prospector_v2_get_scroll_layer_2(void)  { return 0xFF; }
__weak uint16_t prospector_v2_get_scroll_div(void)     { return 0; }
