/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/status_scanner.h>
#include <zmk/status_advertisement.h>

#if IS_ENABLED(CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT)
#include <zmk/status_advertisement_v2.h>
#endif

/* Shared scanner-pending data — single source of truth so the producer
 * (scanner_stub.c) and the consumer (bootstrap.c::refresh_tick) cannot
 * drift. Previously mirrored by hand in two files. */
#define SCANNER_STUB_MAX_NAME_LEN 32

struct pending_display_data {
    volatile bool update_pending;
    volatile bool signal_update_pending;  /* Signal widget updates separately (1Hz) */
    volatile bool no_keyboards;           /* True when all keyboards timed out */

    char device_name[SCANNER_STUB_MAX_NAME_LEN];
    char layer_name[4];                   /* NOT null-terminated */
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

    /* Keyboard firmware version (decoded from version + profile_slot fields) */
    uint8_t kb_version_major;
    uint8_t kb_version_minor;
    uint8_t kb_version_patch;
    bool kb_version_dev;
    bool kb_version_valid;                /* True after first keyboard data received */
};

/**
 * @brief Read & clear the latest pending display data
 *
 * Returns false if no update is queued. On success, copies pending_data
 * into *out and clears the update_pending flag.
 */
bool scanner_get_pending_update(struct pending_display_data *out);

/* Latest RSSI from the BT RX path; produced by scanner_process_incoming(),
 * consumed by the LVGL refresh tick. Volatile because the writer is the
 * 1Hz rate-calc block and the reader runs from a different context. */
extern volatile int8_t scanner_signal_rssi;

/**
 * @brief Send keyboard data received from BLE advertisement
 *
 * Lock-free: pushes data into SPSC ring buffer.
 * Called from BT RX thread. Data is processed by scanner_process_incoming()
 * in the LVGL timer context.
 *
 * @param adv_data Parsed advertisement data
 * @param rssi Signal strength
 * @param device_name Keyboard device name
 * @param ble_addr BLE MAC address (6 bytes)
 * @param ble_addr_type BLE address type
 * @return 0 on success, negative error code on failure
 */
int scanner_msg_send_keyboard_data(const struct zmk_status_adv_data *adv_data,
                                   int8_t rssi, const char *device_name,
                                   const uint8_t *ble_addr, uint8_t ble_addr_type);

/**
 * @brief Process incoming advertisements from ring buffer
 *
 * Must be called from LVGL timer context (main thread).
 * Drains ring buffer, manages keyboards[], calculates rates,
 * checks timeouts, and updates scanner battery.
 */
void scanner_process_incoming(void);

/**
 * @brief Trigger timeout check for keyboards
 *
 * Checks if any keyboards have timed out and updates display accordingly.
 *
 * @return 0 on success, negative error code on failure
 */
int scanner_msg_send_timeout_check(void);

/**
 * @brief Get keyboard data by index
 *
 * @param index Keyboard index
 * @param data Output: advertisement data
 * @param rssi Output: signal strength
 * @param name Output: keyboard name
 * @param name_len Size of name buffer
 * @return true if keyboard found, false otherwise
 */
bool scanner_get_keyboard_data(int index, struct zmk_status_adv_data *data,
                               int8_t *rssi, char *name, size_t name_len);

/**
 * @brief Get the count of active keyboards
 *
 * @return Number of active keyboards
 */
int scanner_get_active_keyboard_count(void);

/**
 * @brief Get keyboard status pointer by index (LVGL timer context only)
 *
 * Returns a direct pointer to the keyboard status struct.
 * Safe because keyboards[] is only accessed from LVGL timer context.
 *
 * @param index Keyboard index (0 to MAX_KEYBOARDS-1)
 * @return Pointer to keyboard status, NULL if inactive or invalid index
 */
struct zmk_keyboard_status *scanner_get_keyboard_status(int index);

/**
 * @brief Get the selected keyboard index
 *
 * @return Selected keyboard index
 */
int scanner_get_selected_keyboard(void);

/**
 * @brief Set the selected keyboard index
 *
 * @param index Keyboard index to select
 */
void scanner_set_selected_keyboard(int index);

/**
 * @brief Send display refresh request
 *
 * Triggers immediate display update after screen transitions to MAIN.
 *
 * @return 0 on success, negative error code on failure
 */
int scanner_msg_send_display_refresh(void);

#if IS_ENABLED(CONFIG_PROSPECTOR_STATUS_ADV_V2_EXT)
/**
 * @brief Push one parsed v2 frame into the v2 ring buffer
 *
 * Lock-free SPSC: called from BT RX thread by status_scanner.c after
 * service-UUID validation. Drained by scanner_pop_pending_v2() from
 * the LVGL timer context, which reassembles frames into the final
 * meteorite_data snapshot.
 *
 * Each v2 packet on the wire is one 26-byte frame; the keyboard cycles
 * through ZMK_STATUS_ADV_V2_FRAME_COUNT frame_ids over time.
 *
 * @param frame v2 frame payload (caller-owned, copied internally)
 * @param rssi  Signal strength (reserved for future use)
 * @return 0 on success, -ENOSPC if the v2 ring is full
 */
int scanner_msg_send_v2_data(const union zmk_status_adv_v2_frame *frame,
                             int8_t rssi);

/**
 * @brief Pop one v2 frame from the ring buffer
 *
 * Returns the oldest queued frame, FIFO order. Unlike the v1 path we
 * cannot collapse to "latest" because different frame_ids carry
 * different fields — each must be applied to the reassembly buffer.
 *
 * @param out Destination (only written when true is returned)
 * @return true if a v2 frame was available, false if the ring was empty
 */
bool scanner_pop_pending_v2(union zmk_status_adv_v2_frame *out);
#endif
