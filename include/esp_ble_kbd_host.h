/*
 * esp_ble_kbd_host.h — BLE HID keyboard host for ESP-IDF (NimBLE)
 *
 * Bundles a modified esp_hid component with lightweight reconnect
 * (esp_ble_hidh_dev_reconnect) and pre-GATT security initiation.
 *
 * All display and GPIO coupling is removed; the application wires
 * callbacks to its own UI layer and calls ble_kbd_host_start_pairing()
 * from whatever input it chooses (BOOT button, menu option, etc).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Scan result
 * -------------------------------------------------------------------------- */

#define BLE_KBD_HOST_MAX_SCAN_DEVS  8
#define BLE_KBD_HOST_NAME_MAX_LEN  32

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    char    name[BLE_KBD_HOST_NAME_MAX_LEN + 1];
    int8_t  rssi;
} ble_kbd_scan_dev_t;

/* --------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------- */

typedef enum {
    BLE_KBD_STATE_IDLE         = 0,
    BLE_KBD_STATE_RECONNECTING,   /* was connected; retrying to reconnect   */
    BLE_KBD_STATE_SCANNING,       /* active BLE scan, awaiting device select */
    BLE_KBD_STATE_CONNECTING,     /* device selected, GATT open in progress  */
    BLE_KBD_STATE_CONNECTED,      /* keyboard fully connected and usable     */
} ble_kbd_state_t;

/* --------------------------------------------------------------------------
 * Callbacks — all fire from the component's internal task.
 * Keep implementations short; do not block.
 * -------------------------------------------------------------------------- */

typedef struct {
    /**
     * Key event. ascii is the translated ASCII character.
     * NUL (0x00) is a synthetic wakeup posted after reconnect — discard it
     * before enqueueing to your own key ring.
     */
    void (*on_key)(char ascii, void *ctx);

    /**
     * State changed. line1/line2 are human-readable status strings, valid
     * only for the duration of the callback.
     */
    void (*on_status)(ble_kbd_state_t state,
                      const char *line1, const char *line2, void *ctx);

    /**
     * Passkey to display during pairing. The user types this 6-digit code
     * on the keyboard being paired.
     */
    void (*on_passkey)(uint32_t passkey, void *ctx);

    /**
     * Scan list updated (~500 ms during SCANNING state).
     * devs[] is a snapshot valid only for this callback's duration.
     * count == 0 while no devices have been found yet.
     */
    void (*on_scan_updated)(const ble_kbd_scan_dev_t *devs,
                            int count, void *ctx);

    /** Passed as the last argument to every callback. */
    void *ctx;
} ble_kbd_callbacks_t;

/* --------------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------------- */

typedef struct {
    ble_kbd_callbacks_t callbacks;

    /**
     * If true, clear all BLE bonds and go straight to scan/pair mode.
     * Typically set when the application detects a BOOT-button hold
     * before calling ble_kbd_host_init().
     */
    bool force_repair;

    /**
     * Internal task tuning. 0 uses Kconfig defaults.
     */
    uint32_t task_stack;             /* default: CONFIG_BLE_KBD_HOST_TASK_STACK   */
    uint8_t  task_priority;          /* default: CONFIG_BLE_KBD_HOST_TASK_PRIORITY */
    uint32_t reconnect_interval_ms;  /* default: CONFIG_BLE_KBD_HOST_RECONNECT_INTERVAL_MS */
    uint32_t scan_timeout_ms;        /* default: CONFIG_BLE_KBD_HOST_SCAN_TIMEOUT_MS */
} ble_kbd_host_config_t;

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * @brief Initialise the BLE HID host and start the connect/pair cycle.
 *
 * Starts NimBLE, esp_hidh, and an internal state-machine task. Must be
 * called once. Returns immediately; status arrives via on_status/on_key.
 *
 * @param cfg  Configuration. Copied internally; pointer need not persist.
 * @return ESP_OK on success.
 */
esp_err_t ble_kbd_host_init(const ble_kbd_host_config_t *cfg);

/**
 * @brief Clear all BLE bonds and enter scan/pair mode.
 *
 * Safe to call from any FreeRTOS task, including an on_key or on_status
 * callback. If a device is currently connected it will be disconnected.
 * Triggers on_status(BLE_KBD_STATE_SCANNING, ...).
 */
void ble_kbd_host_start_pairing(void);

/**
 * @brief Connect to a device from the current scan list.
 *
 * Only meaningful when state == BLE_KBD_STATE_SCANNING.
 * index refers to the devs[] array from the most recent on_scan_updated.
 * Out-of-range index is silently ignored.
 */
void ble_kbd_host_select_device(int index);

/** @return true if state == BLE_KBD_STATE_CONNECTED. */
bool ble_kbd_host_is_connected(void);

/** @return current state machine state. */
ble_kbd_state_t ble_kbd_host_get_state(void);

/**
 * @brief Block until the keyboard reaches CONNECTED state.
 *
 * Useful for app_main() to gate further startup. Waits indefinitely;
 * provide your own timeout wrapper if needed.
 */
void ble_kbd_host_wait_connected(void);

#ifdef __cplusplus
}
#endif
