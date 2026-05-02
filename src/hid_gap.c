/*
 * hid_gap.c — NimBLE GAP initialisation for esp32-ble-kbd-host
 *
 * Stripped from ESP-IDF's esp_hid_gap.c (Espressif Systems, Unlicense/CC0-1.0),
 * retaining only controller + NimBLE host bring-up. The connection-level GAP
 * event handling (passkey display, repeat-pairing, encryption change) lives
 * in nimble_hidh.c's esp_hidh_gattc_event_handler — that's the callback
 * registered on ble_gap_connect / esp_hidh_dev_open, so a second handler here
 * would never fire.
 */

#include "sdkconfig.h"
#include "esp_log.h"

#include "host/ble_hs.h"
#include "nimble/nimble_port.h"

#include "hid_gap.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_hosted_misc.h"
#else
#include "esp_bt.h"
#include "nimble/nimble_port_freertos.h"
extern esp_err_t esp_nimble_init(void);
#endif

static const char *TAG = "hid_gap";

static esp_err_t init_low_level(void)
{
    esp_err_t ret;

#if CONFIG_IDF_TARGET_ESP32P4
    // ESP32-P4: no native BT — BLE runs on C6 co-processor via esp_hosted vHCI
    ret = esp_hosted_bt_controller_init();
    if (ret) {
        ESP_LOGE(TAG, "esp_hosted_bt_controller_init failed: %d", ret);
        return ret;
    }
    ret = esp_hosted_bt_controller_enable();
    if (ret) {
        ESP_LOGE(TAG, "esp_hosted_bt_controller_enable failed: %d", ret);
        return ret;
    }
    ret = nimble_port_init();
    if (ret) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return ret;
    }
#else
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_mem_release failed: %d", ret);
        return ret;
    }
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %d", ret);
        return ret;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %d", ret);
        return ret;
    }
    ret = esp_nimble_init();
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_init failed: %d", ret);
        return ret;
    }
#endif
    return ESP_OK;
}

esp_err_t esp_hid_gap_init(uint8_t mode)
{
    (void)mode;  // NimBLE-only: mode is always BLE, parameter kept for API compat
    return init_low_level();
}
