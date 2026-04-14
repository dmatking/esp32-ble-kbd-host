cmake_minimum_required(VERSION 3.16)

# Bundled modified esp_hid — NimBLE-only sources.
# Bluedroid sources (ble_hidh.c, bt_hidh.c, etc.) are intentionally excluded.
set(ESP_HID_SRCS
    esp_hid/src/esp_hidh.c
    esp_hid/src/esp_hidd.c
    esp_hid/src/esp_hid_common.c
    esp_hid/src/nimble_hidh.c
    esp_hid/src/nimble_hidd.c
)

# esp_hosted is only available (and needed) on ESP32-P4, where BLE is
# provided by a co-processor rather than the main SoC.
set(EXTRA_REQUIRES "")
if("${IDF_TARGET}" STREQUAL "esp32p4")
    list(APPEND EXTRA_REQUIRES espressif__esp_hosted)
endif()

idf_component_register(
    SRCS
        "src/bt_kbd.c"
        "src/hid_gap.c"
        ${ESP_HID_SRCS}
    INCLUDE_DIRS
        "include"
        "esp_hid/include"
    PRIV_INCLUDE_DIRS
        "src"
        "esp_hid/include/esp_private"
        "esp_hid/private"
    REQUIRES
        esp_event
        bt
        driver
        freertos
        esp_system
        esp_wifi
        ${EXTRA_REQUIRES}
)

# Suppress spurious format warnings in the vendored nimble_hidh.c
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
