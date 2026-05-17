/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Status Advertisement v2 — Phase 2 extension. See status_advertisement_v2.h.
 *
 * Runs as a second BLE 5.0 Extended Advertising set, fully independent of
 * the v1 legacy ADV. Cadence is low (~1Hz periodic + immediate on-change),
 * since the v2 payload contains slow-moving metadata (layer names, custom
 * config) rather than fast keyboard state.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <zmk/prospector_compat.h>
#include <zmk/status_advertisement.h>      /* PROSPECTOR_ENCODE_* macros */
#include <zmk/status_advertisement_v2.h>
#include <zmk/prospector_v2_hooks.h>

/* The keymap API is only available on Central or Standalone builds. */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/keymap.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT)

/* ============================== State ============================== */

static struct bt_le_ext_adv *v2_adv_set = NULL;
static struct k_work_delayable v2_adv_work;
static bool v2_started = false;

static struct zmk_status_adv_v2_data v2_payload;

static struct bt_data v2_ad[] = {
    BT_DATA(BT_DATA_MANUFACTURER_DATA,
            (uint8_t *)&v2_payload, sizeof(v2_payload)),
};

/* Cached so we don't re-hash hwinfo every cycle. */
static uint8_t v2_keyboard_id[4] = {0};
static bool    v2_keyboard_id_cached = false;

#define V2_REFRESH_INTERVAL_MS  1000

/* ============================== Helpers ============================ */

static void v2_compute_keyboard_id(void) {
    if (v2_keyboard_id_cached) {
        return;
    }

    /* Identical computation to v1 (status_advertisement.c). Duplicated
     * intentionally — the hash is derived from a hardware constant and
     * never changes at runtime, so caching once is enough and avoids
     * coupling v2 to v1's internal helpers. */
    uint8_t hwid[16];
    ssize_t hwid_len = hwinfo_get_device_id(hwid, sizeof(hwid));
    uint32_t id_hash = 0;

    if (hwid_len > 0) {
        for (ssize_t i = 0; i < hwid_len; i++) {
            id_hash = id_hash * 31 + hwid[i];
        }
    } else {
        const char *kb_name = CONFIG_ZMK_STATUS_ADV_KEYBOARD_NAME;
        if (strlen(kb_name) == 0) {
            kb_name = CONFIG_BT_DEVICE_NAME;
        }
        for (int i = 0; kb_name[i]; i++) {
            id_hash = id_hash * 31 + (uint8_t)kb_name[i];
        }
    }

    memcpy(v2_keyboard_id, &id_hash, sizeof(v2_keyboard_id));
    v2_keyboard_id_cached = true;
}

static void v2_build_layer_names(void) {
    memset(v2_payload.layer_names, 0, sizeof(v2_payload.layer_names));
    v2_payload.layer_count = 0;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    uint8_t cap = ZMK_STATUS_ADV_V2_LAYER_COUNT;
    uint8_t total = (ZMK_KEYMAP_LAYERS_LEN < cap) ? ZMK_KEYMAP_LAYERS_LEN : cap;

    for (uint8_t i = 0; i < total; i++) {
        const char *name = zmk_keymap_layer_name(i);
        if (name && name[0] != '\0') {
            /* Copy up to 4 chars; pad with 0x00 (already memset). */
            size_t n = strnlen(name, ZMK_STATUS_ADV_V2_LAYER_NAME_LEN);
            memcpy(v2_payload.layer_names[i], name, n);
        }
    }
    v2_payload.layer_count = total;
#endif
}

static void v2_build_payload(void) {
    v2_compute_keyboard_id();

    v2_payload.manufacturer_id[0] = 0xFF;
    v2_payload.manufacturer_id[1] = 0xFF;
    v2_payload.service_uuid[0] =
        (ZMK_STATUS_ADV_V2_SERVICE_UUID >> 8) & 0xFF;
    v2_payload.service_uuid[1] =
        ZMK_STATUS_ADV_V2_SERVICE_UUID & 0xFF;
    v2_payload.version = ZMK_STATUS_ADV_V2_VERSION;
    memcpy(v2_payload.keyboard_id, v2_keyboard_id, 4);
    v2_payload.channel = CONFIG_PROSPECTOR_CHANNEL;

    v2_payload.os_mode          = prospector_v2_get_os_mode();
    v2_payload.cpi_value        = prospector_v2_get_cpi();
    v2_payload.scroll_layer_1   = prospector_v2_get_scroll_layer_1();
    v2_payload.scroll_layer_2   = prospector_v2_get_scroll_layer_2();
    v2_payload.scroll_div_value = prospector_v2_get_scroll_div();

    v2_build_layer_names();
}

/* ============================== Work loop ========================== */

static int v2_ensure_adv_set(void) {
    if (v2_adv_set) {
        return 0;
    }

    /* Non-connectable, no name. Created lazily on the first work tick
     * so the BLE stack is fully up (the SYS_INIT hook just queues the
     * delayed work; calling bt_le_ext_adv_create before BT init returns
     * -EAGAIN on some Zephyr versions). */
    const struct bt_le_adv_param param = *BT_LE_EXT_ADV_NCONN;

    int err = bt_le_ext_adv_create(&param, NULL, &v2_adv_set);
    if (err) {
        LOG_WRN("v2 ext adv create deferred: %d", err);
        v2_adv_set = NULL;
        return err;
    }

    LOG_INF("v2 ext adv set created");
    return 0;
}

static void v2_adv_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (v2_ensure_adv_set() != 0) {
        /* Retry on the next tick once BLE finishes coming up. */
        k_work_schedule(&v2_adv_work, K_SECONDS(1));
        return;
    }

    v2_build_payload();

    int err = bt_le_ext_adv_set_data(v2_adv_set,
                                     v2_ad, ARRAY_SIZE(v2_ad),
                                     NULL, 0);
    if (err) {
        LOG_WRN("v2 ext adv set_data failed: %d", err);
    } else if (!v2_started) {
        err = bt_le_ext_adv_start(v2_adv_set, BT_LE_EXT_ADV_START_DEFAULT);
        if (err) {
            LOG_ERR("v2 ext adv start failed: %d", err);
        } else {
            v2_started = true;
            LOG_INF("v2 ext adv started (%u bytes)",
                    (unsigned)sizeof(v2_payload));
        }
    }

    k_work_schedule(&v2_adv_work, K_MSEC(V2_REFRESH_INTERVAL_MS));
}

/* ============================== Public API ========================= */

void zmk_status_advertisement_v2_notify_changed(void) {
    /* Reschedule immediately. cancel+schedule is safe on a delayable. */
    (void)k_work_reschedule(&v2_adv_work, K_NO_WAIT);
}

int zmk_status_advertisement_v2_init(void) {
    static bool inited = false;
    if (inited) {
        return 0;  /* Idempotent */
    }
    inited = true;

    k_work_init_delayable(&v2_adv_work, v2_adv_work_handler);
    /* Defer the first tick by ~1s so the ZMK BLE stack is ready
     * (mirrors the v1 init delay in status_advertisement.c). */
    k_work_schedule(&v2_adv_work, K_SECONDS(1));

    LOG_INF("v2 ext adv init queued");
    return 0;
}

static int v2_sys_init(PROSPECTOR_SYS_INIT_ARGS) {
    PROSPECTOR_SYS_INIT_UNUSED;
    return zmk_status_advertisement_v2_init();
}

/* Same APPLICATION priority as v1 (95). */
SYS_INIT(v2_sys_init, APPLICATION, 95);

#else  /* !CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT */

int zmk_status_advertisement_v2_init(void) { return 0; }
void zmk_status_advertisement_v2_notify_changed(void) {}

#endif
