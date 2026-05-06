#include "wifi_manager.h"
#include "config.h"
#include "watchdog.h"
#include "rtc_clock.h"
#include <WiFi.h>
#include <time.h>

static bool      _ntpSynced  = false;
static uint32_t  _bootMs     = 0;

static void syncNTP() {
    configTzTime(TZ_STRING, NTP_SERVER);
    struct tm t;
    uint8_t tries = 0;
    while (!getLocalTime(&t, 1000) && tries++ < 10) {
        wdt::feed();
        delay(500);
    }
    _ntpSynced = (tries < 10);
    if (_ntpSynced) rtc::writeFromSystem();
}

void wifi::connect() {
    _bootMs = millis();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint8_t tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) {
        wdt::feed();
        delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) syncNTP();
}

void wifi::loop() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 10000) return;
    lastCheck = millis();

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
    } else if (!_ntpSynced) {
        syncNTP();
    }
}

bool wifi::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool wifi::ntpSynced() {
    return _ntpSynced;
}

int8_t wifi::rssi() {
    return WiFi.isConnected() ? (int8_t)WiFi.RSSI() : 0;
}

uint32_t wifi::uptimeSec() {
    return (millis() - _bootMs) / 1000;
}
