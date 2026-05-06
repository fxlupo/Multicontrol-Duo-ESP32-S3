#include "watchdog.h"
#include "config.h"
#include "esp_arduino_version.h"
#include "esp_task_wdt.h"

static bool _wdtStarted = false;
static bool _wdtAttached = false;

void wdt::init() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    esp_task_wdt_config_t cfg = {
        .timeout_ms     = WDT_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic  = true
    };
    esp_err_t err = esp_task_wdt_reconfigure(&cfg);
    if (err != ESP_OK) esp_task_wdt_init(&cfg);
#else
    // Arduino ESP32 2.x / IDF 4.x API
    esp_task_wdt_init(WDT_TIMEOUT_MS / 1000, true);
#endif
    esp_task_wdt_add(NULL);
    _wdtStarted = true;
    _wdtAttached = true;
}

void wdt::feed() {
    if (!_wdtStarted || !_wdtAttached) return;
    esp_task_wdt_reset();
}

void wdt::pause() {
    if (!_wdtStarted || !_wdtAttached) return;
    esp_task_wdt_delete(NULL);
    _wdtAttached = false;
}

void wdt::resume() {
    if (!_wdtStarted || _wdtAttached) return;
    esp_task_wdt_add(NULL);
    _wdtAttached = true;
    feed();
}
