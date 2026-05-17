/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Status Advertisement v2 — multi-frame legacy builder.
 *
 * Provides zmk_status_adv_v2_build_frame() so the existing v1 work
 * handler (status_advertisement.c) can splice v2 frames into its own
 * piggyback / proxy / MODE2 transmissions, on a slow rotating cadence.
 *
 * No BLE host APIs are touched here — this file only knows how to
 * marshal one v2 frame into the 26-byte wire format. The actual
 * bt_le_adv_* call is the v1 path's responsibility.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <zmk/status_advertisement_v2.h>
#include <zmk/prospector_v2_hooks.h>

/* Layer name API only on Central/Standalone — peripherals lack it. */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/keymap.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT)

static void fill_header(struct zmk_status_adv_v2_header *hdr,
                        uint8_t frame_id,
                        const uint8_t keyboard_id[4]) {
    hdr->mfg_id[0] = 0xFF;
    hdr->mfg_id[1] = 0xFF;
    hdr->service_uuid[0] = (ZMK_STATUS_ADV_V2_SERVICE_UUID >> 8) & 0xFF;
    hdr->service_uuid[1] =  ZMK_STATUS_ADV_V2_SERVICE_UUID       & 0xFF;
    hdr->ver_frame = ZMK_STATUS_ADV_V2_ENCODE_VER_FRAME(
                         ZMK_STATUS_ADV_V2_VERSION, frame_id);
    memcpy(hdr->keyboard_id, keyboard_id, 4);
}

static void build_config_frame(struct zmk_status_adv_v2_config *cfg,
                               const uint8_t keyboard_id[4]) {
    memset(cfg, 0, sizeof(*cfg));
    fill_header(&cfg->hdr, ZMK_STATUS_ADV_V2_FRAME_CONFIG, keyboard_id);

    cfg->os_mode          = prospector_v2_get_os_mode();
    cfg->cpi_value        = prospector_v2_get_cpi();
    cfg->scroll_layer_1   = prospector_v2_get_scroll_layer_1();
    cfg->scroll_layer_2   = prospector_v2_get_scroll_layer_2();
    cfg->scroll_div_value = prospector_v2_get_scroll_div();
}

static void build_layers_frame(struct zmk_status_adv_v2_layers *lay,
                               const uint8_t keyboard_id[4],
                               uint8_t frame_id) {
    memset(lay, 0, sizeof(*lay));
    fill_header(&lay->hdr, frame_id, keyboard_id);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    /* Layer-group index 0 covers layers [0..3], 1 covers [4..7]. */
    uint8_t group = (frame_id == ZMK_STATUS_ADV_V2_FRAME_LAYERS_A) ? 0 : 1;
    uint8_t base  = group * ZMK_STATUS_ADV_V2_LAYERS_PER_FRAME;

    uint8_t total = ZMK_KEYMAP_LAYERS_LEN;
    if (total > ZMK_STATUS_ADV_V2_MAX_LAYERS) {
        total = ZMK_STATUS_ADV_V2_MAX_LAYERS;
    }
    lay->layer_count_total = total;

    for (uint8_t i = 0; i < ZMK_STATUS_ADV_V2_LAYERS_PER_FRAME; i++) {
        uint8_t layer = base + i;
        if (layer >= total) break;

        const char *name = zmk_keymap_layer_name(layer);
        if (name && name[0] != '\0') {
            size_t n = strnlen(name, ZMK_STATUS_ADV_V2_LAYER_NAME_LEN);
            memcpy(lay->names[i], name, n);
            /* remainder is already 0 from memset */
        }
    }
#endif
}

void zmk_status_adv_v2_build_frame(uint8_t frame_id,
                                   const uint8_t keyboard_id[4],
                                   union zmk_status_adv_v2_frame *out) {
    BUILD_ASSERT(sizeof(struct zmk_status_adv_v2_config) ==
                 ZMK_STATUS_ADV_V2_FRAME_SIZE,
                 "v2 config frame must be 26 bytes");
    BUILD_ASSERT(sizeof(struct zmk_status_adv_v2_layers) ==
                 ZMK_STATUS_ADV_V2_FRAME_SIZE,
                 "v2 layers frame must be 26 bytes");

    switch (frame_id) {
    case ZMK_STATUS_ADV_V2_FRAME_CONFIG:
        build_config_frame(&out->config, keyboard_id);
        break;
    case ZMK_STATUS_ADV_V2_FRAME_LAYERS_A:
    case ZMK_STATUS_ADV_V2_FRAME_LAYERS_B:
        build_layers_frame(&out->layers, keyboard_id, frame_id);
        break;
    default:
        LOG_WRN("v2 build: unknown frame_id %u", (unsigned)frame_id);
        memset(out, 0, sizeof(*out));
        fill_header(&out->hdr, 0xF, keyboard_id);  /* mark invalid */
        break;
    }
}

#else  /* !CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT */

void zmk_status_adv_v2_build_frame(uint8_t frame_id,
                                   const uint8_t keyboard_id[4],
                                   union zmk_status_adv_v2_frame *out) {
    ARG_UNUSED(frame_id);
    ARG_UNUSED(keyboard_id);
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}

#endif
