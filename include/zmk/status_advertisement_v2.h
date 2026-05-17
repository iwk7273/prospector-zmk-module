/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Status Advertisement v2 — Phase 2 (multi-frame legacy redesign).
 *
 * The original v2 design used a 60-byte BLE 5.0 Extended Advertising set
 * parallel to the v1 legacy ADV. That design proved incompatible with
 * the upstream prospector v1 piggyback path: enabling CONFIG_BT_EXT_ADV
 * alone (without any ext-adv set being created) silences the legacy
 * adv_update_data() flow, killing v1. The prospector-zmk-module Kconfig
 * comment "Uses legacy advertising API - no BT_EXT_ADV needed" was
 * load-bearing.
 *
 * New design (Phase 2-redesign):
 *   - v2 stays on the same legacy ADV channel as v1, sharing the same
 *     piggyback / proxy / MODE2 transport.
 *   - The keyboard's adv_work_handler multiplexes: 1 out of every N ticks
 *     advertises a v2 frame instead of v1. Defaults to ~10% v2 bandwidth
 *     so v1's fast keyboard-state updates aren't starved.
 *   - v2 is split across 3 fixed 26-byte frames (1 config + 2 layer
 *     groups, covering 8 layers total). Service UUID 0xABCE distinguishes
 *     them from v1 (0xABCD), so old scanners ignore v2 packets cleanly.
 *   - Each v2 frame carries a frame_id so the scanner can reassemble
 *     into a single snapshot.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_STATUS_ADV_V2_SERVICE_UUID  0xABCE
#define ZMK_STATUS_ADV_V2_VERSION       0x1     /* upper nibble of ver_frame */

/* 3 frames total: 1 config + 2 layer-name groups (4 names per group). */
#define ZMK_STATUS_ADV_V2_FRAME_COUNT       3
#define ZMK_STATUS_ADV_V2_FRAME_CONFIG      0
#define ZMK_STATUS_ADV_V2_FRAME_LAYERS_A    1   /* layers 0..3 */
#define ZMK_STATUS_ADV_V2_FRAME_LAYERS_B    2   /* layers 4..7 */

#define ZMK_STATUS_ADV_V2_LAYER_NAME_LEN    4   /* NOT null-terminated */
#define ZMK_STATUS_ADV_V2_LAYERS_PER_FRAME  4
#define ZMK_STATUS_ADV_V2_MAX_LAYERS \
    (ZMK_STATUS_ADV_V2_LAYERS_PER_FRAME * (ZMK_STATUS_ADV_V2_FRAME_COUNT - 1))
                                                /* = 8 — extend by adding
                                                 * a 4th frame if more is
                                                 * ever needed. */

/* All v2 frames share this 13-byte header. */
struct zmk_status_adv_v2_header {
    uint8_t mfg_id[2];          /* 0xFF 0xFF */
    uint8_t service_uuid[2];    /* 0xAB 0xCE */
    uint8_t ver_frame;          /* [7:4]=version, [3:0]=frame_id */
    uint8_t keyboard_id[4];     /* matches v1 keyboard_id */
} __packed;                     /* = 9 bytes */

/* Frame 0 — keyboard-specific custom config. */
struct zmk_status_adv_v2_config {
    struct zmk_status_adv_v2_header hdr;        /* 9 bytes */
    uint8_t  os_mode;                           /* 0=WIN,1=MAC,0xFF=unknown */
    uint16_t cpi_value;                         /* LE, 0=unknown */
    uint8_t  scroll_layer_1;                    /* 0xFF=disabled */
    uint8_t  scroll_layer_2;                    /* 0xFF=disabled */
    uint16_t scroll_div_value;                  /* LE, 0=unknown */
    uint8_t  reserved[8];                       /* pad to 26 */
} __packed;                                     /* = 26 bytes */

/* Frame 1/2 — layer names in groups of 4. */
struct zmk_status_adv_v2_layers {
    struct zmk_status_adv_v2_header hdr;        /* 9 bytes */
    uint8_t layer_count_total;                  /* total across ALL frames */
    char    names[ZMK_STATUS_ADV_V2_LAYERS_PER_FRAME]
                 [ZMK_STATUS_ADV_V2_LAYER_NAME_LEN];
                                                /* 16 bytes */
} __packed;                                     /* = 26 bytes */

/* Tagged-union convenience type — same 26-byte wire payload either way. */
union zmk_status_adv_v2_frame {
    struct zmk_status_adv_v2_header hdr;        /* common prefix */
    struct zmk_status_adv_v2_config config;
    struct zmk_status_adv_v2_layers layers;
    uint8_t bytes[26];
};

#define ZMK_STATUS_ADV_V2_FRAME_SIZE  26

/**
 * Populate a v2 frame for the given frame_id.
 *
 * Reads the keyboard-specific custom config via the weak getters in
 * prospector_v2_hooks.h, and layer names via zmk_keymap_layer_name().
 *
 * @param frame_id    0..ZMK_STATUS_ADV_V2_FRAME_COUNT-1
 * @param keyboard_id 4-byte hwinfo hash, must match the keyboard_id used
 *                    by the v1 packet so scanners can correlate the two.
 * @param out         destination, written in full
 */
void zmk_status_adv_v2_build_frame(uint8_t frame_id,
                                   const uint8_t keyboard_id[4],
                                   union zmk_status_adv_v2_frame *out);

/* Decode helpers (scanner side) */
#define ZMK_STATUS_ADV_V2_DECODE_VERSION(vf)   (((vf) >> 4) & 0x0F)
#define ZMK_STATUS_ADV_V2_DECODE_FRAME_ID(vf)  ((vf) & 0x0F)
#define ZMK_STATUS_ADV_V2_ENCODE_VER_FRAME(version, frame_id) \
    ((((version) & 0x0F) << 4) | ((frame_id) & 0x0F))

#ifdef __cplusplus
}
#endif
