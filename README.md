# esp32-ble-kbd-host

BLE HID keyboard host component for ESP-IDF (NimBLE stack).

Handles pairing, reconnection, and key decoding with a clean callback API.
No display or GPIO coupling — works headless or with any UI layer.

## Features

- **Automatic reconnection** — reconnects to the last bonded keyboard without re-pairing
- **Reliable re-pair** — `ble_kbd_host_start_pairing()` escapes from any state, including
  the reconnect loop (fixes a common watchdog bug where BOOT hold was ignored mid-retry)
- **Single persistent task** — one `kbd_main_task` state machine replaces the
  scan-task → watchdog hand-off pattern that caused missed BOOT presses
- **Callback API** — on_key, on_status, on_passkey, on_scan_updated; no queue returned,
  no display pointer, no GPIO in the component
- **Bundled modified esp_hid** — includes `esp_ble_hidh_dev_reconnect()` (lightweight
  reconnect without full GATT rediscovery), passkey handler, enc-change handler,
  device kept alive on disconnect
- **ESP32-P4 support** — BLE via ESP32-C6 co-processor through `esp_hosted` / vHCI

## Quick start

In your project's `main/idf_component.yml`:

```yaml
dependencies:
  dmatking/esp32-ble-kbd-host: ">=1.0.0"
```

For local development, use a path dependency instead:

```yaml
dependencies:
  esp32-ble-kbd-host:
    path: /path/to/esp32-ble-kbd-host
```

```c
#include "esp_ble_kbd_host.h"

static QueueHandle_t s_key_queue;

static void on_key(char ascii, void *ctx) {
    if (ascii == '\0') return;   // synthetic wakeup — discard
    xQueueSend(s_key_queue, &ascii, 0);
}

static void on_status(ble_kbd_state_t state,
                      const char *line1, const char *line2, void *ctx) {
    ESP_LOGI("kbd", "state %d: %s / %s", state, line1, line2);
}

void app_main(void) {
    s_key_queue = xQueueCreate(64, sizeof(char));

    ble_kbd_host_config_t cfg = {
        .callbacks = {
            .on_key    = on_key,
            .on_status = on_status,
            .ctx       = NULL,
        },
    };
    ESP_ERROR_CHECK(ble_kbd_host_init(&cfg));

    // Block until keyboard connects (optional)
    ble_kbd_host_wait_connected();

    char c;
    while (1) {
        if (xQueueReceive(s_key_queue, &c, portMAX_DELAY)) {
            printf("key: %c\n", c);
        }
    }
}
```

## API

```c
// Initialise — starts the internal kbd_main_task
esp_err_t ble_kbd_host_init(const ble_kbd_host_config_t *cfg);

// Trigger re-pair from any state (safe to call from any task)
void ble_kbd_host_start_pairing(void);

// Select device by index during SCANNING state
void ble_kbd_host_select_device(int index);

// Query state
bool            ble_kbd_host_is_connected(void);
ble_kbd_state_t ble_kbd_host_get_state(void);

// Block until CONNECTED (useful in app_main before starting SSH etc.)
void ble_kbd_host_wait_connected(void);
```

## Config (`menuconfig`)

| Key | Default | Description |
|-----|---------|-------------|
| `BLE_KBD_HOST_TASK_STACK` | 8192 | kbd_main_task stack bytes |
| `BLE_KBD_HOST_TASK_PRIORITY` | 2 | kbd_main_task priority |
| `BLE_KBD_HOST_RECONNECT_INTERVAL_MS` | 3000 | Delay between reconnect attempts |
| `BLE_KBD_HOST_SCAN_TIMEOUT_MS` | 20000 | BLE scan window before restart |

## State machine

```
IDLE
  └─ has bonds? ──yes──► RECONNECTING ─── reconnect succeeds ───► CONNECTED
                │                 │                                    │
                │                 └── CMD_START_PAIRING ──►            │
                no                                        │        disconnect
                │                                         ▼            │
                └─────────────────────────────────► SCANNING           │
                                                         │              │
                                               device selected          │
                                                         ▼              │
                                                    CONNECTING          │
                                                         │              │
                                                    open succeeds       │
                                                         └──────────────┘
```

`CMD_START_PAIRING` (from `ble_kbd_host_start_pairing()`) clears bonds and forces
a scan regardless of current state. This is what makes BOOT hold reliable from
the RECONNECTING state — the previous watchdog implementation polled BOOT only in
the scan loop, so holding BOOT while retrying a reconnect had no effect.

## License

Apache-2.0. Bundled `esp_hid/` sources are from Espressif Systems
(Unlicense / Apache-2.0); see file headers for details.
