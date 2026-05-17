/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Meteorite Layout bootstrap.
 *
 * Replaces the multi-screen custom_status_screen.c with a single fixed
 * meteorite_layout. Owns:
 *   - the LVGL screen object returned by zmk_display_status_screen()
 *   - a periodic LVGL timer that drains scanner_stub pending data,
 *     pushes it into meteorite_data, and calls meteorite_layout_refresh().
 *
 * There is no swipe navigation, no settings UI, no multi-keyboard select.
 * Phase 2 / 3 transports add data via meteorite_data setters and do not need
 * to touch this file.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <string.h>

#include <zmk/display/status_screen.h>
#include <zmk/status_advertisement.h>
#if IS_ENABLED(CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT)
#include <zmk/status_advertisement_v2.h>
#endif

#include "meteorite_layout.h"
#include "meteorite_data.h"
#include "scanner_stub.h"

LOG_MODULE_REGISTER(meteorite_bootstrap, LOG_LEVEL_INF);

/* ========== Mirror of scanner_stub.c::pending_display_data ==========
 * Kept in lock-step with the producer struct in scanner_stub.c. If fields
 * are added there, mirror them here. Same pattern as the original
 * custom_status_screen.c (the producer doesn't export the struct). */

#define MAX_NAME_LEN 32
struct pending_display_data {
    volatile bool update_pending;
    volatile bool signal_update_pending;
    volatile bool no_keyboards;

    char device_name[MAX_NAME_LEN];
    char layer_name[4];           /* NOT null-terminated */
    int layer;
    int wpm;
    bool usb_ready;
    bool ble_connected;
    bool ble_bonded;
    int profile;
    uint8_t modifiers;
    int bat[4];
    int8_t rssi;
    float rate_hz;
    int scanner_battery;
    bool scanner_battery_pending;

    uint8_t kb_version_major;
    uint8_t kb_version_minor;
    uint8_t kb_version_patch;
    bool kb_version_dev;
    bool kb_version_valid;
};

extern bool scanner_get_pending_update(struct pending_display_data *out);
extern volatile int8_t scanner_signal_rssi;

/* ========== Refresh tick ========== */

#define REFRESH_PERIOD_MS 100

static lv_timer_t *refresh_timer = NULL;
static int8_t      last_known_rssi = -127;

static void apply_pending_to_data(const struct pending_display_data *p) {
    if (p->no_keyboards) {
        meteorite_data_clear_keyboard();
        return;
    }

    uint8_t periph[METEORITE_PERIPHERAL_COUNT];
    for (int i = 0; i < METEORITE_PERIPHERAL_COUNT; i++) {
        /* scanner_stub stores central in bat[0], peripherals in bat[1..3] */
        periph[i] = (uint8_t)(p->bat[1 + i] & 0xFF);
    }

    char layer_name[METEORITE_LAYER_NAME_LEN] = {0};
    memcpy(layer_name, p->layer_name, sizeof(p->layer_name));
    layer_name[sizeof(p->layer_name)] = '\0';

    meteorite_data_set_keyboard_v1(
        p->device_name,
        last_known_rssi,
        (uint8_t)(p->bat[0] & 0xFF),
        periph,
        (uint8_t)(p->layer & 0xFF),
        layer_name,
        p->usb_ready,
        p->ble_connected,
        p->ble_bonded,
        (uint8_t)(p->profile & 0xFF)
    );
}

#if IS_ENABLED(CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT)
static void apply_v2_to_data(const struct zmk_status_adv_v2_data *v2) {
    /* Layer list: copy into a (count × 5) char[][] so meteorite_data can
     * accept null-terminated entries. v2 packs 4 chars per slot without
     * a terminator. */
    char names[METEORITE_MAX_LAYERS][METEORITE_LAYER_NAME_LEN] = {{0}};
    uint8_t count = v2->layer_count;
    if (count > METEORITE_MAX_LAYERS) {
        count = METEORITE_MAX_LAYERS;
    }
    for (uint8_t i = 0; i < count; i++) {
        memcpy(names[i], v2->layer_names[i],
               ZMK_STATUS_ADV_V2_LAYER_NAME_LEN);
        names[i][METEORITE_LAYER_NAME_LEN - 1] = '\0';
    }
    meteorite_data_set_layer_list(count, names);

    meteorite_data_set_custom_config(
        v2->os_mode,
        v2->cpi_value,
        v2->scroll_layer_1,
        v2->scroll_layer_2,
        v2->scroll_div_value
    );
}
#endif

static void refresh_tick(lv_timer_t *t) {
    ARG_UNUSED(t);

    /* RSSI updates arrive on a separate flag; capture the latest. */
    last_known_rssi = scanner_signal_rssi;

    struct pending_display_data p;
    if (scanner_get_pending_update(&p)) {
        apply_pending_to_data(&p);
    }

#if IS_ENABLED(CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT)
    struct zmk_status_adv_v2_data v2;
    if (scanner_get_pending_v2(&v2)) {
        apply_v2_to_data(&v2);
    }
#endif

    meteorite_layout_refresh();
}

/* ========== ZMK display entry ========== */

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 280, 240);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    meteorite_data_init();
    meteorite_layout_create(screen);

    if (!refresh_timer) {
        refresh_timer = lv_timer_create(refresh_tick, REFRESH_PERIOD_MS, NULL);
    }

    LOG_INF("Meteorite status screen ready");
    return screen;
}
