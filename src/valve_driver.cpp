#include "valve_driver.h"
#include "config.h"
#include <Preferences.h>

#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 1
#endif

#ifndef RELAY_ZONE_COUNT
#define RELAY_ZONE_COUNT 6
#endif

#if RELAY_ZONE_COUNT < 1 || RELAY_ZONE_COUNT > 8
#error "RELAY_ZONE_COUNT must be between 1 and 8."
#endif

static const uint8_t ALL_RELAY_PINS[8] = {
    V1_IN1, V1_IN2, V2_IN1, V2_IN2, V3_IN1, V3_IN2, V4_IN1, V4_IN2
};

struct ValveState {
    bool          open;
    unsigned long openedAt;
    uint32_t      maxMs;
};

static ValveState    _state[RELAY_ZONE_COUNT] = {};
static volatile bool _busy = false;
static unsigned long _openLockoutUntil = 0;

static valve::Settings _settings = {
    VALVE_OPEN_PULSE_MS,
    VALVE_CLOSE_PULSE_MS,
    VALVE_CLOSE_DUTY,
    VALVE_SEQ_PAUSE_MS
};

static uint16_t clampU16(uint16_t v, uint16_t lo, uint16_t hi) {
    return min<uint16_t>(hi, max<uint16_t>(lo, v));
}

static uint8_t relayInactiveLevel() {
    return RELAY_ACTIVE_LOW ? HIGH : LOW;
}

static uint8_t relayActiveLevel() {
    return RELAY_ACTIVE_LOW ? LOW : HIGH;
}

static void writeRelay(uint8_t idx, bool active) {
    if (idx >= RELAY_ZONE_COUNT) return;
    digitalWrite(ALL_RELAY_PINS[idx], active ? relayActiveLevel() : relayInactiveLevel());
}

static void allRelaysOff() {
    for (uint8_t i = 0; i < RELAY_ZONE_COUNT; i++) {
        writeRelay(i, false);
    }
}

static void clearState(uint8_t idx) {
    if (idx >= RELAY_ZONE_COUNT) return;
    _state[idx].open = false;
    _state[idx].openedAt = 0;
    _state[idx].maxMs = 0;
}

static void saveSettings() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUShort("v_open_ms", _settings.openPulseMs);
    p.putUShort("v_close_ms", _settings.closePulseMs);
    p.putUChar("v_close_duty", _settings.closeDuty);
    p.putUShort("v_seq_ms", _settings.seqPauseMs);
    p.end();
}

static void loadSettings() {
    Preferences p;
    p.begin(NVS_NAMESPACE, true);
    _settings.openPulseMs  = clampU16(p.getUShort("v_open_ms", VALVE_OPEN_PULSE_MS), 50, 1000);
    _settings.closePulseMs = clampU16(p.getUShort("v_close_ms", VALVE_CLOSE_PULSE_MS), 1, 1000);
    _settings.closeDuty    = min<uint8_t>(255, max<uint8_t>(1, p.getUChar("v_close_duty", VALVE_CLOSE_DUTY)));
    _settings.seqPauseMs   = clampU16(p.getUShort("v_seq_ms", VALVE_SEQ_PAUSE_MS), 0, 5000);
    p.end();
}

void valve::init() {
    loadSettings();

    for (uint8_t i = 0; i < RELAY_ZONE_COUNT; i++) {
        pinMode(ALL_RELAY_PINS[i], OUTPUT);
        digitalWrite(ALL_RELAY_PINS[i], relayInactiveLevel());
        clearState(i);
    }

    _busy = false;
}

void valve::closeAll() {
    _busy = true;
    allRelaysOff();
    for (uint8_t i = 0; i < RELAY_ZONE_COUNT; i++) {
        clearState(i);
    }
    _busy = false;
}

bool valve::open(uint8_t zone, uint32_t maxDurationMs) {
    if (zone < 1 || zone > RELAY_ZONE_COUNT || _busy) return false;
    uint8_t idx = zone - 1;
    if (_state[idx].open) return true;
    if (_openLockoutUntil != 0 && (long)(millis() - _openLockoutUntil) < 0) return false;
    if (getOpenZone() > 0) return false;

    _busy = true;
    allRelaysOff();
    writeRelay(idx, true);
    _state[idx] = {true, millis(), maxDurationMs};
    _busy = false;
    return true;
}

void valve::close(uint8_t zone) {
    if (zone < 1 || zone > RELAY_ZONE_COUNT) return;
    uint8_t idx = zone - 1;

    _busy = true;
    writeRelay(idx, false);
    clearState(idx);
    _busy = false;
}

void valve::update() {
    for (uint8_t i = 0; i < RELAY_ZONE_COUNT; i++) {
        if (_state[i].open && _state[i].maxMs > 0) {
            if (millis() - _state[i].openedAt >= _state[i].maxMs) {
                close(i + 1);
            }
        }
    }
}

void valve::lockOpens(uint32_t durationMs) {
    _openLockoutUntil = millis() + durationMs;
}

bool valve::isOpen(uint8_t zone) {
    if (zone < 1 || zone > RELAY_ZONE_COUNT) return false;
    return _state[zone - 1].open;
}

uint8_t valve::getOpenZone() {
    for (uint8_t i = 0; i < RELAY_ZONE_COUNT; i++) {
        if (_state[i].open) return i + 1;
    }
    return 0;
}

String valve::stateStr() {
    String s;
    s.reserve(RELAY_ZONE_COUNT);
    for (uint8_t i = 0; i < RELAY_ZONE_COUNT; i++) {
        s += '0';
    }
    for (uint8_t i = 0; i < RELAY_ZONE_COUNT; i++) {
        if (_state[i].open) s[i] = '1';
    }
    return s;
}

valve::Settings valve::settings() {
    return _settings;
}

void valve::setOpenPulseMs(uint16_t ms) {
    _settings.openPulseMs = clampU16(ms, 50, 1000);
    saveSettings();
}

void valve::setClosePulseMs(uint16_t ms) {
    _settings.closePulseMs = clampU16(ms, 1, 1000);
    saveSettings();
}

void valve::setCloseDuty(uint8_t duty) {
    _settings.closeDuty = min<uint8_t>(255, max<uint8_t>(1, duty));
    saveSettings();
}

void valve::setSeqPauseMs(uint16_t ms) {
    _settings.seqPauseMs = clampU16(ms, 0, 5000);
    saveSettings();
}

void valve::resetSettings() {
    _settings = {VALVE_OPEN_PULSE_MS, VALVE_CLOSE_PULSE_MS, VALVE_CLOSE_DUTY, VALVE_SEQ_PAUSE_MS};
    saveSettings();
}
