#include "rtc_clock.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include <sys/time.h>

static bool _rtcAvailable = false;

static uint8_t bcdToDec(uint8_t v) {
    return ((v >> 4) * 10) + (v & 0x0F);
}

static uint8_t decToBcd(uint8_t v) {
    return ((v / 10) << 4) | (v % 10);
}

static bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)RTC_I2C_ADDR, (uint8_t)len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static bool writeRegs(uint8_t reg, const uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(reg);
    for (uint8_t i = 0; i < len; i++) Wire.write(buf[i]);
    return Wire.endTransmission() == 0;
}

static bool validLocalTime(const tm& t) {
    return t.tm_year >= 124 && t.tm_year <= 150 &&
           t.tm_mon >= 0 && t.tm_mon <= 11 &&
           t.tm_mday >= 1 && t.tm_mday <= 31 &&
           t.tm_hour >= 0 && t.tm_hour <= 23 &&
           t.tm_min >= 0 && t.tm_min <= 59 &&
           t.tm_sec >= 0 && t.tm_sec <= 59;
}

void rtc::init() {
    Wire.begin(RTC_SDA, RTC_SCL);
    uint8_t status = 0;
    _rtcAvailable = readRegs(0x0F, &status, 1);
    if (_rtcAvailable) {
        Serial.println("DS3231 RTC gefunden.");
    } else {
        Serial.println("DS3231 RTC nicht gefunden.");
    }
}

bool rtc::available() {
    return _rtcAvailable;
}

bool rtc::syncSystemFromRtc() {
    if (!_rtcAvailable) return false;

    uint8_t r[7] = {};
    if (!readRegs(0x00, r, sizeof(r))) return false;

    tm t = {};
    t.tm_sec  = bcdToDec(r[0] & 0x7F);
    t.tm_min  = bcdToDec(r[1] & 0x7F);
    t.tm_hour = bcdToDec(r[2] & 0x3F);
    t.tm_mday = bcdToDec(r[4] & 0x3F);
    t.tm_mon  = bcdToDec(r[5] & 0x1F) - 1;
    t.tm_year = bcdToDec(r[6]) + 100;
    t.tm_isdst = -1;
    if (!validLocalTime(t)) return false;

    setenv("TZ", TZ_STRING, 1);
    tzset();
    time_t epoch = mktime(&t);
    if (epoch < 1700000000) return false;

    timeval tv = {.tv_sec = epoch, .tv_usec = 0};
    settimeofday(&tv, nullptr);
    Serial.println("Systemzeit aus DS3231 gesetzt.");
    return true;
}

bool rtc::writeFromSystem() {
    if (!_rtcAvailable) return false;

    tm t;
    if (!getLocalTime(&t, 100)) return false;
    if (!validLocalTime(t)) return false;

    uint8_t r[7] = {
        decToBcd((uint8_t)t.tm_sec),
        decToBcd((uint8_t)t.tm_min),
        decToBcd((uint8_t)t.tm_hour),
        decToBcd((uint8_t)(t.tm_wday == 0 ? 7 : t.tm_wday)),
        decToBcd((uint8_t)t.tm_mday),
        decToBcd((uint8_t)(t.tm_mon + 1)),
        decToBcd((uint8_t)(t.tm_year - 100)),
    };
    if (!writeRegs(0x00, r, sizeof(r))) return false;

    uint8_t status = 0;
    if (readRegs(0x0F, &status, 1)) {
        status &= ~0x80; // OSF löschen
        writeRegs(0x0F, &status, 1);
    }
    Serial.println("DS3231 aus Systemzeit aktualisiert.");
    return true;
}
