#include "ota_update.h"
#include "config.h"
#include "wifi_manager.h"
#include "watchdog.h"
#include <ArduinoOTA.h>

static bool _active = false;
static bool _running = false;
static bool _begun = false;
static uint8_t _progress = 0;
static uint32_t _untilMs = 0;
static const char* _status = "bereit";

void ota::init() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        _running = true;
        _progress = 0;
        _status = "Upload startet";
        wdt::pause();
    });
    ArduinoOTA.onEnd([]() {
        _running = false;
        _progress = 100;
        _status = "Neustart";
        wdt::resume();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        if (total == 0) return;
        _progress = (uint8_t)min<unsigned int>(100, progress * 100U / total);
        _status = "Upload laeuft";
    });
    ArduinoOTA.onError([](ota_error_t error) {
        _running = false;
        wdt::resume();
        switch (error) {
            case OTA_AUTH_ERROR:    _status = "Auth Fehler"; break;
            case OTA_BEGIN_ERROR:   _status = "Start Fehler"; break;
            case OTA_CONNECT_ERROR: _status = "Netz Fehler"; break;
            case OTA_RECEIVE_ERROR: _status = "Empfang Fehler"; break;
            case OTA_END_ERROR:     _status = "Ende Fehler"; break;
            default:                _status = "OTA Fehler"; break;
        }
    });
}

void ota::startWindow(uint32_t windowMs) {
    if (!wifi::isConnected()) {
        _status = "kein WiFi";
        return;
    }
    if (!_begun) {
        ArduinoOTA.begin();
        _begun = true;
    }
    _active = true;
    _running = false;
    _progress = 0;
    _untilMs = millis() + windowMs;
    _status = "warte auf Upload";
}

void ota::stop() {
    if (_begun && !_running) {
        ArduinoOTA.end();
        _begun = false;
    }
    _active = false;
    _running = false;
    _progress = 0;
    _untilMs = 0;
    _status = "bereit";
}

void ota::handle() {
    if (!_active) return;
    ArduinoOTA.handle();
    if (!_running && _untilMs != 0 && (int32_t)(millis() - _untilMs) >= 0) {
        stop();
    }
}

bool ota::active() {
    return _active;
}

bool ota::running() {
    return _running;
}

uint8_t ota::progress() {
    return _progress;
}

uint32_t ota::secondsLeft() {
    if (!_active || _untilMs == 0) return 0;
    int32_t left = (int32_t)(_untilMs - millis());
    return left > 0 ? (uint32_t)left / 1000UL : 0;
}

const char* ota::statusText() {
    return _status;
}

const char* ota::hostname() {
    return OTA_HOSTNAME;
}
