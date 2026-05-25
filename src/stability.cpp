#include "stability.h"
#include "config.h"
#include <esp_attr.h>
#include <Preferences.h>

#ifndef STABILITY_SNAPSHOT_MS
#define STABILITY_SNAPSHOT_MS 300000UL
#endif

static esp_reset_reason_t _resetReason = ESP_RST_UNKNOWN;
static uint32_t _bootCount = 0;
static uint32_t _crashCount = 0;
static uint32_t _previousUptimeSec = 0;
static uint32_t _minFreeHeap = 0;
static uint32_t _lastBreadcrumbUptimeSec = 0;
static uint32_t _lastBreadcrumbHeap = 0;
static unsigned long _lastSnapshotMs = 0;
static char _bootDetail[64] = "";
static char _lastBreadcrumbStage[24] = "";

struct Breadcrumb {
    uint32_t magic;
    uint32_t seq;
    uint32_t uptimeSec;
    uint32_t heap;
    char stage[24];
};

static constexpr uint32_t BREADCRUMB_MAGIC = 0xB22C0DED;
RTC_NOINIT_ATTR static Breadcrumb _breadcrumb;

static bool isCrashReason(esp_reset_reason_t reason) {
    return reason == ESP_RST_PANIC ||
           reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_TASK_WDT ||
           reason == ESP_RST_WDT ||
           reason == ESP_RST_BROWNOUT;
}

static void saveSnapshot() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.putUInt("uptime_sec", millis() / 1000UL);
    prefs.putUInt("min_heap", _minFreeHeap);
    prefs.end();
}

void stability::init() {
    _resetReason = esp_reset_reason();
    _minFreeHeap = ESP.getFreeHeap();
    _lastSnapshotMs = millis();

    if (_breadcrumb.magic == BREADCRUMB_MAGIC) {
        _lastBreadcrumbUptimeSec = _breadcrumb.uptimeSec;
        _lastBreadcrumbHeap = _breadcrumb.heap;
        strlcpy(_lastBreadcrumbStage, _breadcrumb.stage, sizeof(_lastBreadcrumbStage));
    }

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        _bootCount = prefs.getUInt("boot_count", 0) + 1;
        _crashCount = prefs.getUInt("crash_count", 0);
        _previousUptimeSec = prefs.getUInt("uptime_sec", 0);
        if (isCrashReason(_resetReason)) _crashCount++;

        prefs.putUInt("boot_count", _bootCount);
        prefs.putUInt("crash_count", _crashCount);
        prefs.putUInt("last_reason", (uint32_t)_resetReason);
        prefs.putUInt("min_heap", _minFreeHeap);
        prefs.end();
    }

    snprintf(_bootDetail, sizeof(_bootDetail), "R %s B%lu C%lu U%lu S %s H%lu",
             stability::resetReasonCode(),
             (unsigned long)_bootCount,
             (unsigned long)_crashCount,
             (unsigned long)_previousUptimeSec,
             _lastBreadcrumbStage[0] ? _lastBreadcrumbStage : "-",
             (unsigned long)_lastBreadcrumbHeap);
    stability::mark("setup:init");
}

void stability::update() {
    uint32_t heap = ESP.getFreeHeap();
    if (_minFreeHeap == 0 || heap < _minFreeHeap) _minFreeHeap = heap;

    unsigned long now = millis();
    if (now - _lastSnapshotMs >= STABILITY_SNAPSHOT_MS) {
        saveSnapshot();
        _lastSnapshotMs = now;
    }
}

void stability::mark(const char* stage) {
    _breadcrumb.magic = BREADCRUMB_MAGIC;
    _breadcrumb.seq++;
    _breadcrumb.uptimeSec = millis() / 1000UL;
    _breadcrumb.heap = ESP.getFreeHeap();
    strlcpy(_breadcrumb.stage, stage ? stage : "-", sizeof(_breadcrumb.stage));
}

esp_reset_reason_t stability::resetReason() {
    return _resetReason;
}

const char* stability::resetReasonText() {
    switch (_resetReason) {
        case ESP_RST_POWERON:   return "Power-On";
        case ESP_RST_EXT:       return "Externer Reset";
        case ESP_RST_SW:        return "Software-Reset";
        case ESP_RST_PANIC:     return "Panic/Exception";
        case ESP_RST_INT_WDT:   return "Interrupt-Watchdog";
        case ESP_RST_TASK_WDT:  return "Task-Watchdog";
        case ESP_RST_WDT:       return "Watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep-Sleep";
        case ESP_RST_BROWNOUT:  return "Brownout";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "Unbekannt";
    }
}

const char* stability::resetReasonCode() {
    switch (_resetReason) {
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_EXT:       return "external";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

bool stability::resetWasCrash() {
    return isCrashReason(_resetReason);
}

uint32_t stability::bootCount() {
    return _bootCount;
}

uint32_t stability::crashCount() {
    return _crashCount;
}

uint32_t stability::previousUptimeSec() {
    return _previousUptimeSec;
}

uint32_t stability::minFreeHeap() {
    return _minFreeHeap;
}

const char* stability::bootEventDetail() {
    return _bootDetail;
}

const char* stability::lastBreadcrumbStage() {
    return _lastBreadcrumbStage;
}

uint32_t stability::lastBreadcrumbUptimeSec() {
    return _lastBreadcrumbUptimeSec;
}

uint32_t stability::lastBreadcrumbHeap() {
    return _lastBreadcrumbHeap;
}
