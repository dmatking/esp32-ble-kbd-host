/*
 * hid_gap.c — NimBLE GAP initialisation for esp-ble-kbd-host
 *
 * Stripped from ESP-IDF's esp_hid_gap.c (Espressif Systems, Unlicense/CC0-1.0),
 * retaining only the NimBLE/BLE paths. Bluedroid and Classic BT paths removed.
 *
 * Provides:
 *   esp_hid_gap_init()           — initialise the BLE controller + NimBLE host
 *   esp_hid_gap_set_passkey_cb() — register passkey-display callback
 */

#include "sdkconfig.h"
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "hid_gap.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "host/ble_hs_adv.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nimble/ble.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_hosted_misc.h"
#else
#include "esp_bt.h"
#include "nimble/nimble_port_freertos.h"
extern esp_err_t esp_nimble_init(void);
#endif

static const char *TAG = "hid_gap";

static void (*s_passkey_cb)(uint32_t) = NULL;
static SemaphoreHandle_t s_ble_cb_sem = NULL;

void esp_hid_gap_set_passkey_cb(void (*cb)(uint32_t passkey))
{
    s_passkey_cb = cb;
}

// ---------------------------------------------------------------------------
// GAP event handler — handles passkey display and repeat-pairing during scans
// ---------------------------------------------------------------------------

static int nimble_hid_gap_event(struct ble_gap_event *event, void *arg)
{
    int rc;
    struct ble_gap_conn_desc desc;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "discovery complete; reason=%d", event->disc_complete.reason);
        if (s_ble_cb_sem)
            xSemaphoreGive(s_ble_cb_sem);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        (void)rc;
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        ESP_LOGI(TAG, "PASSKEY_ACTION_EVENT");
        struct ble_sm_io pkey = {0};
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action  = event->passkey.params.action;
            pkey.passkey = esp_random() % 1000000;
            ESP_LOGI(TAG, ">>> PASSKEY: %06" PRIu32 " <<<", pkey.passkey);
            if (s_passkey_cb) s_passkey_cb(pkey.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "ble_sm_inject_io: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            pkey.action       = event->passkey.params.action;
            pkey.numcmp_accept = 1;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            pkey.action   = event->passkey.params.action;
            pkey.passkey  = 123456;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        return 0;
    }

    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Low-level BLE controller + NimBLE init (target-specific)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Public: initialise GAP
// ---------------------------------------------------------------------------

esp_err_t esp_hid_gap_init(uint8_t mode)
{
    (void)mode;  // NimBLE-only: mode is always BLE, parameter kept for API compat

    if (s_ble_cb_sem != NULL) {
        ESP_LOGE(TAG, "Already initialised");
        return ESP_FAIL;
    }

    s_ble_cb_sem = xSemaphoreCreateBinary();
    if (!s_ble_cb_sem) {
        ESP_LOGE(TAG, "xSemaphoreCreateBinary failed");
        return ESP_ERR_NO_MEM;
    }

    return init_low_level();
}

// ---------------------------------------------------------------------------
// GAP callback accessor — used by nimble_hidh.c to register its own handler
// on top of ours. Not exposed in the public header but referenced internally.
// ---------------------------------------------------------------------------

ble_gap_event_fn *esp_hid_gap_get_event_cb(void)
{
    return nimble_hid_gap_event;
}
