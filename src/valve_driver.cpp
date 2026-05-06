#include "valve_driver.h"
#include "config.h"
#include "watchdog.h"
#include "esp_arduino_version.h"
#include <Preferences.h>

static const uint8_t PINS[4][2] = {
    {V1_IN1, V1_IN2},
    {V2_IN1, V2_IN2},
    {V3_IN1, V3_IN2},
    {V4_IN1, V4_IN2},
};

struct ValveState {
    bool          open;
    unsigned long openedAt;
    uint32_t      maxMs;
};
static ValveState    _state[4] = {};
static volatile bool _busy     = false;
static valve::Settings _settings = {
    VALVE_OPEN_PULSE_MS,
    VALVE_CLOSE_PULSE_MS,
    VALVE_CLOSE_DUTY,
    VALVE_SEQ_PAUSE_MS
};

static uint16_t clampU16(uint16_t v, uint16_t lo, uint16_t hi) {
    return min<uint16_t>(hi, max<uint16_t>(lo, v));
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
    _settings.closePulseMs = clampU16(p.getUShort("v_close_ms", VALVE_CLOSE_PULSE_MS), 20, 1000);
    _settings.closeDuty    = min<uint8_t>(255, max<uint8_t>(1, p.getUChar("v_close_duty", VALVE_CLOSE_DUTY)));
    _settings.seqPauseMs   = clampU16(p.getUShort("v_seq_ms", VALVE_SEQ_PAUSE_MS), 0, 5000);
    p.end();
}

static void delayWithWdt(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) {
        wdt::feed();
        delay(min<uint32_t>(100, ms - (millis() - start)));
    }
}

static void pwmClose(uint8_t pin, uint32_t ms) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(pin, VALVE_CLOSE_PWM_FREQ, VALVE_CLOSE_PWM_BITS);
    ledcWrite(pin, _settings.closeDuty);
    delayWithWdt(ms);
    ledcDetach(pin);
#else
    ledcSetup(0, VALVE_CLOSE_PWM_FREQ, VALVE_CLOSE_PWM_BITS);
    ledcAttachPin(pin, 0);
    ledcWrite(0, VALVE_CLOSE_DUTY);
    delayWithWdt(ms);
    ledcDetachPin(pin);
#endif
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}

static void sendClosePulse(uint8_t in1, uint8_t in2) {
    digitalWrite(in1, LOW);
#ifdef VALVE_TEST_DIRECT_CLOSE
    digitalWrite(in2, HIGH);
    delayWithWdt(_settings.closePulseMs);
    digitalWrite(in2, LOW);
#else
    pwmClose(in2, _settings.closePulseMs);
#endif
}

static void sendOpenPulse(uint8_t in1, uint8_t in2) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
    delayWithWdt(_settings.openPulseMs);
    digitalWrite(in1, LOW);
}

void valve::init() {
    loadSettings();
    for (uint8_t i = 0; i < 4; i++) {
        pinMode(PINS[i][0], OUTPUT);
        pinMode(PINS[i][1], OUTPUT);
        digitalWrite(PINS[i][0], LOW);
        digitalWrite(PINS[i][1], LOW);
    }
    closeAll();
}

void valve::closeAll() {
    for (uint8_t i = 0; i < 4; i++) {
        sendClosePulse(PINS[i][0], PINS[i][1]);
        _state[i].open = false;
        if (i < 3) delayWithWdt(_settings.seqPauseMs);
    }
    _busy = false;
}

bool valve::open(uint8_t zone, uint32_t maxDurationMs) {
    if (zone < 1 || zone > 4 || _busy) return false;
    uint8_t idx = zone - 1;
    if (_state[idx].open) return true;
    _busy = true;
    sendOpenPulse(PINS[idx][0], PINS[idx][1]);
    _state[idx] = {true, millis(), maxDurationMs};
    _busy = false;
    return true;
}

void valve::close(uint8_t zone) {
    if (zone < 1 || zone > 4) return;
    uint8_t idx = zone - 1;
    if (!_state[idx].open) return;
    _busy = true;
    sendClosePulse(PINS[idx][0], PINS[idx][1]);
    _state[idx].open = false;
    _busy = false;
}

void valve::update() {
    for (uint8_t i = 0; i < 4; i++) {
        if (_state[i].open && _state[i].maxMs > 0) {
            if (millis() - _state[i].openedAt >= _state[i].maxMs) {
                close(i + 1);
            }
        }
    }
}

bool valve::isOpen(uint8_t zone) {
    if (zone < 1 || zone > 4) return false;
    return _state[zone - 1].open;
}

uint8_t valve::getOpenZone() {
    for (uint8_t i = 0; i < 4; i++) if (_state[i].open) return i + 1;
    return 0;
}

String valve::stateStr() {
    String s = "0000";
    for (uint8_t i = 0; i < 4; i++) if (_state[i].open) s[i] = '1';
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
    _settings.closePulseMs = clampU16(ms, 20, 1000);
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
