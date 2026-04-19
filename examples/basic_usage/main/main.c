/*
 * basic_usage — esp32-ble-kbd-host example
 *
 * Initialises the BLE HID keyboard host and logs received keystrokes.
 * On first run (no bonded device) the component scans and this example
 * auto-selects the first keyboard found.  Subsequent boots reconnect
 * automatically without pairing.
 *
 * Target: any ESP32 with NimBLE support (S3, C6, P4+esp_hosted, ...).
 * See sdkconfig.defaults for the required BLE Kconfig settings.
 */

#include "esp_ble_kbd_host.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "basic_usage";

// ---------------------------------------------------------------------------
// Callbacks — called from the component's internal task; keep them short.
// ---------------------------------------------------------------------------

static void on_key(char ascii, void *ctx)
{
    if (ascii == '\0') return;  // synthetic wakeup sentinel — discard
    ESP_LOGI(TAG, "key: 0x%02x  '%c'", (uint8_t)ascii,
             (ascii >= 32 && ascii < 127) ? ascii : '?');
}

static void on_status(ble_kbd_state_t state,
                      const char *line1, const char *line2, void *ctx)
{
    ESP_LOGI(TAG, "state %d: %s / %s", state, line1, line2);
}

static void on_passkey(uint32_t passkey, void *ctx)
{
    ESP_LOGI(TAG, "pairing passkey: %06lu — type on keyboard + Enter",
             (unsigned long)passkey);
}

static void on_scan_updated(const ble_kbd_scan_dev_t *devs,
                            int count, void *ctx)
{
    if (count == 0) return;
    ESP_LOGI(TAG, "scan: %d device(s) — selecting '%s'", count, devs[0].name);
    ble_kbd_host_select_device(0);
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ble_kbd_host_config_t cfg = {
        .callbacks = {
            .on_key          = on_key,
            .on_status       = on_status,
            .on_passkey      = on_passkey,
            .on_scan_updated = on_scan_updated,
            .ctx             = NULL,
        },
    };
    ESP_ERROR_CHECK(ble_kbd_host_init(&cfg));

    ESP_LOGI(TAG, "BLE keyboard host started — waiting for keyboard");
    ble_kbd_host_wait_connected();
    ESP_LOGI(TAG, "Keyboard connected — press keys, check the log");

    // The component's internal task keeps running after app_main returns.
}
