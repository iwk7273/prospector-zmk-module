/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include "meteorite_data.h"

#include <zephyr/kernel.h>
#include <string.h>

static struct meteorite_snapshot store;
static struct k_spinlock store_lock;

void meteorite_data_init(void) {
    memset(&store, 0, sizeof(store));
    store.ble_profile_slot = 0;
    store.os_mode = METEORITE_OS_UNKNOWN;
    store.scroll_layer_1 = 0xFF;
    store.scroll_layer_2 = 0xFF;
    /* rate[*].valid stays false until host_rate_rx delivers a sample. */
}

void meteorite_data_snapshot(struct meteorite_snapshot *out) {
    if (!out) return;
    k_spinlock_key_t key = k_spin_lock(&store_lock);
    *out = store;
    k_spin_unlock(&store_lock, key);
}

void meteorite_data_set_keyboard_v1(
    const char *name,
    int8_t rssi,
    uint8_t battery_central,
    const uint8_t peripheral_battery[METEORITE_PERIPHERAL_COUNT],
    uint8_t active_layer,
    const char active_layer_name[METEORITE_LAYER_NAME_LEN],
    bool usb_connected,
    bool ble_connected,
    bool ble_bonded,
    uint8_t ble_profile_slot
) {
    k_spinlock_key_t key = k_spin_lock(&store_lock);

    store.has_keyboard = true;
    if (name) {
        strncpy(store.keyboard_name, name, METEORITE_KB_NAME_LEN - 1);
        store.keyboard_name[METEORITE_KB_NAME_LEN - 1] = '\0';
    }
    store.rssi_dbm = rssi;
    store.battery_central = battery_central;
    if (peripheral_battery) {
        memcpy(store.battery_peripheral, peripheral_battery,
               sizeof(store.battery_peripheral));
    }
    store.active_layer = active_layer;
    if (active_layer_name) {
        memcpy(store.active_layer_name, active_layer_name,
               METEORITE_LAYER_NAME_LEN);
        store.active_layer_name[METEORITE_LAYER_NAME_LEN - 1] = '\0';
    }
    store.usb_connected = usb_connected;
    store.ble_connected = ble_connected;
    store.ble_bonded = ble_bonded;
    store.ble_profile_slot = ble_profile_slot;

    k_spin_unlock(&store_lock, key);
}

void meteorite_data_set_layer_list(uint8_t count,
                                   const char names[][METEORITE_LAYER_NAME_LEN]) {
    if (count > METEORITE_MAX_LAYERS) count = METEORITE_MAX_LAYERS;

    k_spinlock_key_t key = k_spin_lock(&store_lock);
    store.has_layer_list = true;
    store.layer_count = count;
    if (names) {
        memcpy(store.layer_names, names,
               (size_t)count * METEORITE_LAYER_NAME_LEN);
    }
    /* zero-fill the remaining slots so stale names don't bleed through */
    for (uint8_t i = count; i < METEORITE_MAX_LAYERS; i++) {
        store.layer_names[i][0] = '\0';
    }
    k_spin_unlock(&store_lock, key);
}

void meteorite_data_set_custom_config(uint8_t os_mode,
                                      uint16_t cpi_value,
                                      uint8_t scroll_layer_1,
                                      uint8_t scroll_layer_2,
                                      uint16_t scroll_div_value) {
    k_spinlock_key_t key = k_spin_lock(&store_lock);
    store.has_custom_config = true;
    store.os_mode = os_mode;
    store.cpi_value = cpi_value;
    store.scroll_layer_1 = scroll_layer_1;
    store.scroll_layer_2 = scroll_layer_2;
    store.scroll_div_value = scroll_div_value;
    k_spin_unlock(&store_lock, key);
}

void meteorite_data_set_rate_limit(enum meteorite_rate_source src,
                                   uint8_t pct_5h, uint8_t pct_weekly,
                                   uint32_t sec_until_reset_5h,
                                   uint32_t sec_until_reset_w) {
    if (src >= METEORITE_RATE_COUNT) return;
    k_spinlock_key_t key = k_spin_lock(&store_lock);
    store.rate[src].valid = true;
    store.rate[src].pct_5h = pct_5h;
    store.rate[src].pct_weekly = pct_weekly;
    store.rate[src].sec_until_5h = sec_until_reset_5h;
    store.rate[src].sec_until_w = sec_until_reset_w;
    store.rate[src].captured_at_ms = k_uptime_get_32();
    k_spin_unlock(&store_lock, key);
}

void meteorite_data_clear_keyboard(void) {
    k_spinlock_key_t key = k_spin_lock(&store_lock);
    store.has_keyboard = false;
    store.has_layer_list = false;
    store.has_custom_config = false;
    k_spin_unlock(&store_lock, key);
}
