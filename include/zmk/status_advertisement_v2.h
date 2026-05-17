/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Status Advertisement v2 — Phase 2 extension.
 *
 * The v1 packet (status_advertisement.h) is a 26-byte legacy ADV that
 * carries high-cadence keyboard state (battery, active layer, modifiers,
 * RSSI source data). It stays unchanged for backward compatibility.
 *
 * v2 is broadcast as a second, independent BLE 5.0 Extended Advertising
 * set at a low cadence (~1Hz + on-change). It carries:
 *   - full layer name list (all layers, not just active)
 *   - keyboard-specific custom config (OS mode, CPI, scroll layers)
 *
 * Service UUID is bumped to 0xABCE so v1-only scanners ignore v2 packets.
 * The 4-byte keyboard_id matches the v1 packet, letting the scanner
 * correlate the two streams to the same keyboard.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_STATUS_ADV_V2_SERVICE_UUID  0xABCE
#define ZMK_STATUS_ADV_V2_VERSION       0x10  /* major=1, minor=0 */

#define ZMK_STATUS_ADV_V2_LAYER_COUNT   10
#define ZMK_STATUS_ADV_V2_LAYER_NAME_LEN 4    /* NOT null-terminated */

/**
 * v2 packet payload. ~60 bytes — well within the 255-byte Extended
 * Advertising data limit. Fixed-size for simplicity (variable-length
 * encoding deferred until a real need shows up).
 *
 * Field order is chosen so the most-likely-to-change values (custom config,
 * which the user toggles via Studio) sit at low offsets where decoding is
 * cheap, and the static layer-name block sits at the end.
 */
struct zmk_status_adv_v2_data {
    uint8_t  manufacturer_id[2];   /* 0xFF 0xFF — same as v1 */
    uint8_t  service_uuid[2];      /* 0xAB 0xCE — distinct from v1 (0xABCD) */
    uint8_t  version;              /* ZMK_STATUS_ADV_V2_VERSION */
    uint8_t  keyboard_id[4];       /* hwinfo hash; matches v1 packet */
    uint8_t  channel;              /* same channel filter as v1 */

    /* --- Meteorite custom config (7 bytes) --- */
    uint8_t  os_mode;              /* 0=WIN, 1=MAC, 0xFF=unknown */
    uint16_t cpi_value;            /* LE; 0=unknown */
    uint8_t  scroll_layer_1;       /* layer index, 0xFF=disabled */
    uint8_t  scroll_layer_2;       /* layer index, 0xFF=disabled */
    uint16_t scroll_div_value;     /* LE; 0=unknown */

    /* --- Layer name list (41 bytes, fixed) --- */
    uint8_t  layer_count;          /* 0..ZMK_STATUS_ADV_V2_LAYER_COUNT */
    char     layer_names[ZMK_STATUS_ADV_V2_LAYER_COUNT]
                        [ZMK_STATUS_ADV_V2_LAYER_NAME_LEN];
                                   /* NOT null-terminated; pad with 0x00 */
} __packed;  /* = 60 bytes */

/**
 * Initialize the v2 Extended Advertising set and start broadcasting.
 *
 * Idempotent: safe to call multiple times. No-op when
 * CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT is disabled.
 *
 * @return 0 on success, negative errno on failure.
 */
int zmk_status_advertisement_v2_init(void);

/**
 * Request a payload rebuild + retransmit on the next cycle.
 *
 * Called from change hooks (e.g. custom-config-changed) so the new value
 * appears within one cycle instead of waiting for the periodic refresh.
 */
void zmk_status_advertisement_v2_notify_changed(void);

#ifdef __cplusplus
}
#endif
