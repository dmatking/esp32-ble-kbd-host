#pragma once

#include "esp_err.h"
#include <stdint.h>

#define HIDH_BLE_MODE 0x01

// Initialise the BLE controller and NimBLE host.
// mode is ignored (always BLE); kept for API compatibility.
esp_err_t esp_hid_gap_init(uint8_t mode);
