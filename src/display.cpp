#include "display.h"
#include "config.h"
#include "config_sync.h"
#include "valve_driver.h"
#include "ecowitt_client.h"
#include "wifi_manager.h"
#include "event_logger.h"
#include "rtc_clock.h"
#include "ota_update.h"
#include "watchdog.h"
#include "scheduler.h"
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include "esp_arduino_version.h"

// ── Farben (RGB565) ───────────────────────────────────────
#define COL_BG      0x0000   // schwarz
#define COL_SURFACE 0x18C3   // dunkelgrau ~#181818
#define COL_TEXT    0xFFFF   // weiß
#define COL_TEXT2   0x8410   // grau ~#808080
#define COL_MUTED   0xC618   // hellgrau ~#C0C0C0
#define COL_ACCENT  0xBFE7   // #BAFF39 yellow-green
#define COL_RED     0xF810   // #FF4444
#define COL_AMBER   0xFDC0   // #FFB800
#define COL_BORDER  0x4208   // dunkelgrau

// ── Display & Touch ───────────────────────────────────────
#define DISPLAY_ROTATION 2
#define TOUCH_CAL_KEY    "touch_cal_r2"
#define TOUCH_NATIVE_KEY "touch_nat_r2"

static TFT_eSPI* tft = nullptr;
static uint8_t  _screen     = 0;   // 0=Dashboard 1=History 2=Manuell 3=System 4=Diagnose 5=Eventlog 6=OTA 7=Ausgaenge 8=Touch
static uint8_t  _manDur     = 30;  // Manuelle Dauer in Minuten
static bool     _smoothFonts = false;
static bool     _touchNative = false;

// ── Backlight ─────────────────────────────────────────────
static unsigned long _lastTouchMs = 0;
static bool          _dimmed      = false;

#ifndef TFT_BACKLIGHT_ON
#define TFT_BACKLIGHT_ON HIGH
#endif

static void blSet(uint8_t brightness) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, brightness > 0 ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
}
static void blFull() {
    blSet(DISPLAY_BL_FULL);
    _dimmed = false;
    _lastTouchMs = millis();
}
static void blDim() {
    blSet(DISPLAY_BL_DIM);
    _dimmed = true;
}

// ── Touch-Kalibrierung ────────────────────────────────────
static void saveTouchCalibration(const uint16_t cal[5]) {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putBytes(TOUCH_CAL_KEY, cal, sizeof(uint16_t) * 5);
    p.putBool(TOUCH_NATIVE_KEY, true);
    p.end();
    _touchNative = true;
}

static void loadCalibration() {
    Preferences p;
    p.begin(NVS_NAMESPACE, true);
    uint16_t cal[5] = {};
    bool ok = p.getBytes(TOUCH_CAL_KEY, cal, sizeof(cal)) == sizeof(cal);
    _touchNative = p.getBool(TOUCH_NATIVE_KEY, ok);
    p.end();
    if (ok) {
        tft->setTouch(cal);
    } else {
        Serial.println("Touch calibration missing for rotation 2; starting calibration.");
        tft->fillScreen(COL_BG);
        tft->setTextColor(COL_TEXT, COL_BG);
        tft->setTextSize(1);
        tft->setCursor(16, 28);
        tft->print("Touch neu kalibrieren");
        tft->setCursor(16, 48);
        tft->print("Markierungen antippen");
        delay(700);
        wdt::pause();
        tft->calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 15);
        wdt::resume();
        tft->setTouch(cal);
        saveTouchCalibration(cal);
    }
}

static void runTouchCalibration() {
    uint16_t cal[5] = {};
    tft->fillScreen(COL_BG);
    tft->setTextColor(COL_TEXT, COL_BG);
    tft->setTextSize(1);
    tft->setCursor(16, 28);
    tft->print("Touch-Kalibrierung");
    tft->setCursor(16, 48);
    tft->print("Markierungen antippen");
    delay(500);

    wdt::pause();
    tft->calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 15);
    wdt::resume();
    tft->setTouch(cal);

    saveTouchCalibration(cal);

    tft->fillScreen(COL_BG);
    tft->setTextColor(COL_ACCENT, COL_BG);
    tft->setCursor(16, 42);
    tft->print("Kalibrierung gespeichert");
    delay(900);
}

// ── Hilfsfunktionen ───────────────────────────────────────
static void drawHLine(int16_t y, uint16_t col) {
    tft->drawFastHLine(0, y, 240, col);
}

static void drawDashedHLine(int16_t y, uint16_t col) {
    for (int16_t x = 8; x < 232; x += 8) {
        tft->drawFastHLine(x, y, 4, col);
    }
}

static void initSmoothFonts() {
    _smoothFonts = SPIFFS.begin(false) &&
                   SPIFFS.exists("/InterReg11.vlw") &&
                   SPIFFS.exists("/InterSemi12.vlw");
    Serial.println(_smoothFonts ? "Inter Smooth Fonts bereit." : "Inter Smooth Fonts nicht verfuegbar.");
}

static void drawHeaderTextFallback(const char* title, const char* timeText) {
    tft->setTextColor(COL_ACCENT, COL_SURFACE);
    tft->setTextSize(1);
    tft->setCursor(8, 9);
    tft->print(title);

    tft->setCursor(202, 9);
    tft->setTextColor(COL_TEXT, COL_SURFACE);
    tft->print(timeText);
}

static void drawHeaderTextSmooth(const char* title, const char* timeText) {
    tft->loadFont("InterSemi12", SPIFFS);
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(COL_ACCENT, COL_SURFACE, true);
    tft->drawString(title, 8, 7);

    tft->setTextDatum(TR_DATUM);
    tft->setTextColor(COL_TEXT, COL_SURFACE, true);
    tft->drawString(timeText, 236, 7);
    tft->unloadFont();
    tft->setTextDatum(TL_DATUM);
}

static const ecowitt::SoilData* soilForChannel(uint8_t channel) {
    if (channel < 1 || channel > ECOWITT_MAX_SOIL_CHANNELS) return nullptr;
    return &ecowitt::soil[channel - 1];
}

static void drawNavBar() {
    const char* labels[] = {"Dashboard", "Sensor", "Manuell"};
    const int16_t y = 292;
    const int16_t h = 25;
    if (_smoothFonts) {
        tft->loadFont("InterReg11", SPIFFS);
        tft->setTextDatum(MC_DATUM);
    }
    for (uint8_t i = 0; i < 3; i++) {
        int16_t x = i * 80;
        uint16_t col = (i == _screen) ? COL_ACCENT : COL_BORDER;
        tft->drawRect(x, y, 80, h, col);
        if (_smoothFonts) {
            tft->setTextColor((i == _screen) ? COL_ACCENT : COL_MUTED, COL_BG, true);
            tft->drawString(labels[i], x + 40, y + h / 2 + 1);
        } else {
            tft->setTextColor((i == _screen) ? COL_ACCENT : COL_MUTED, COL_BG);
            tft->setTextSize(1);
            int16_t textX = x + (80 - (int16_t)strlen(labels[i]) * 6) / 2;
            int16_t textY = y + (h - 8) / 2;
            tft->setCursor(textX, textY);
            tft->print(labels[i]);
        }
    }
    if (_smoothFonts) {
        tft->unloadFont();
        tft->setTextDatum(TL_DATUM);
    }
}

static const char* valveLabel(uint8_t zone) {
    return valve::isOpen(zone) ? "OFFEN" : "AUS";
}

static String tftText(const char* text) {
    String out;
    if (!text) return out;

    while (*text) {
        uint8_t c = (uint8_t)*text++;
        if (c == 0xC3 && *text) {
            uint8_t d = (uint8_t)*text++;
            switch (d) {
                case 0x84: out += "Ae"; break;
                case 0x96: out += "Oe"; break;
                case 0x9C: out += "Ue"; break;
                case 0xA4: out += "ae"; break;
                case 0xB6: out += "oe"; break;
                case 0xBC: out += "ue"; break;
                case 0x9F: out += "ss"; break;
                default: break;
            }
        } else {
            out += (char)c;
        }
    }

    return out;
}

static String zoneLabel(uint8_t index, const char* fallbackPrefix = "Zone ") {
    if (index < cfg::zoneCount && cfg::zones[index].name[0]) {
        return tftText(cfg::zones[index].name);
    }
    return String(fallbackPrefix) + String(index + 1);
}

static uint8_t displayZoneCount() {
    return min<uint8_t>(RELAY_ZONE_COUNT, cfg::MAX_ZONES);
}

static const ZoneConfig* zoneConfigAt(uint8_t index) {
    if (index < cfg::zoneCount) return &cfg::zones[index];
    return nullptr;
}

static uint8_t zoneIdAt(uint8_t index) {
    const ZoneConfig* z = zoneConfigAt(index);
    return z ? z->id : (uint8_t)(index + 1);
}

static uint8_t zoneSensorChannel(uint8_t index) {
    const ZoneConfig* z = zoneConfigAt(index);
    return z ? z->wh52_channel : (uint8_t)(index + 1);
}

static bool zoneActiveAt(uint8_t index) {
    const ZoneConfig* z = zoneConfigAt(index);
    return z ? z->active : true;
}

static uint8_t relayGpioForIndex(uint8_t index) {
    static const uint8_t gpios[] = {V1_IN1, V1_IN2, V2_IN1, V2_IN2, V3_IN1, V3_IN2, V4_IN1, V4_IN2};
    return index < sizeof(gpios) ? gpios[index] : 0;
}

static String fitTextWidth(const String& text, int16_t maxWidth) {
    if (!tft || tft->textWidth(text) <= maxWidth) return text;

    String out = text;
    while (out.length() > 1 && tft->textWidth(out + ".") > maxWidth) {
        out.remove(out.length() - 1);
    }
    return out + ".";
}

// Bit0=Mo..Bit6=So; tm_wday: 0=So,1=Mo..6=Sa
static bool scheduleWeekdayMatch(uint8_t mask, int tmWday) {
    int bit = (tmWday == 0) ? 6 : tmWday - 1;
    return (mask >> bit) & 1;
}

static String nextScheduleLine(uint8_t zoneId) {
    struct tm nowTm;
    if (!getLocalTime(&nowTm, 50)) return "> Uhr offen";

    int nowMin = nowTm.tm_hour * 60 + nowTm.tm_min;
    const Schedule* best = nullptr;
    int bestDelta = 8 * 24 * 60;
    int bestDayOffset = 0;

    for (uint8_t si = 0; si < cfg::schedCount; si++) {
        const Schedule& s = cfg::schedules[si];
        if (!s.active || s.zone_id != zoneId) continue;

        for (uint8_t day = 0; day < 8; day++) {
            int targetWday = (nowTm.tm_wday + day) % 7;
            if (!scheduleWeekdayMatch(s.weekdays, targetWday)) continue;
            int schedMin = s.start_hour * 60 + s.start_min;
            int delta = (int)day * 24 * 60 + schedMin - nowMin;
            if (delta <= 0) continue;
            if (delta < bestDelta) {
                bestDelta = delta;
                best = &s;
                bestDayOffset = day;
            }
            break;
        }
    }

    if (!best) return "> kein Programm";

    const char* dayLabel = "";
    if (bestDayOffset == 0) {
        dayLabel = "heute";
    } else if (bestDayOffset == 1) {
        dayLabel = "morgen";
    } else {
        static const char* days[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
        dayLabel = days[(nowTm.tm_wday + bestDayOffset) % 7];
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "> %s %02u:%02u %umin",
             dayLabel, best->start_hour, best->start_min, best->duration_min);
    return String(buf);
}

static String currentSkipReason(const ZoneConfig& z) {
    if (!z.active) return "Skip: Zone inaktiv";
    if (!ecowitt::isFresh(z.wh52_channel)) return "Skip: keine Sensor Daten";

    const ecowitt::SoilData& sd = ecowitt::soil[z.wh52_channel - 1];
    if (!isnan(sd.temp) && sd.temp < z.temp_minimum) {
        char buf[28];
        snprintf(buf, sizeof(buf), "Skip: Boden zu kalt %.1fC", sd.temp);
        return String(buf);
    }
    if (!isnan(sd.moisture) && sd.moisture >= z.moisture_threshold) {
        char buf[28];
        snprintf(buf, sizeof(buf), "Skip: Boden zu feucht %.0f%%", sd.moisture);
        return String(buf);
    }
    if (ecowitt::weather.hasRain && !isnan(ecowitt::weather.rain_6h) &&
        ecowitt::weather.rain_6h >= z.rain_threshold_6h) {
        char buf[28];
        snprintf(buf, sizeof(buf), "Skip: es regnet %.1fmm", ecowitt::weather.rain_6h);
        return String(buf);
    }
    return "Plan: wird laufen";
}

static String currentSkipReason(uint8_t index) {
    const ZoneConfig* z = zoneConfigAt(index);
    if (!z) return "Manuell bereit";
    return currentSkipReason(*z);
}

static void drawWifiIcon(int16_t x, int16_t y, bool ok) {
    uint16_t col = ok ? COL_ACCENT : COL_RED;
    tft->fillCircle(x + 5, y + 10, 1, col);
    tft->drawFastHLine(x + 3, y + 7, 5, col);
    tft->drawFastHLine(x + 1, y + 4, 9, col);
    if (!ok) {
        tft->drawLine(x + 1, y + 1, x + 10, y + 10, col);
        tft->drawLine(x + 10, y + 1, x + 1, y + 10, col);
    }
}

static void drawGatewayIcon(int16_t x, int16_t y, bool ok) {
    uint16_t col = ok ? COL_ACCENT : COL_RED;
    tft->drawRect(x + 1, y + 4, 10, 7, col);
    tft->drawFastVLine(x + 6, y + 1, 3, col);
    tft->drawPixel(x + 6, y, col);
    tft->drawPixel(x + 3, y + 7, col);
    tft->drawPixel(x + 8, y + 7, col);
    if (!ok) {
        tft->drawLine(x + 1, y + 1, x + 11, y + 11, col);
        tft->drawLine(x + 11, y + 1, x + 1, y + 11, col);
    }
}

static void drawGearIcon(int16_t cx, int16_t cy) {
    const uint16_t col = COL_MUTED;
    const uint16_t bg = COL_BG;

    tft->drawCircle(cx, cy, 6, col);
    tft->drawCircle(cx, cy, 5, col);
    tft->fillCircle(cx, cy, 3, bg);
    tft->drawCircle(cx, cy, 2, col);

    for (uint8_t i = 0; i < 8; i++) {
        float a = i * PI / 4.0f;
        int16_t x1 = cx + (int16_t)roundf(cosf(a) * 5.0f);
        int16_t y1 = cy + (int16_t)roundf(sinf(a) * 5.0f);
        int16_t x2 = cx + (int16_t)roundf(cosf(a) * 7.0f);
        int16_t y2 = cy + (int16_t)roundf(sinf(a) * 7.0f);
        tft->drawLine(x1, y1, x2, y2, col);
    }
}

static void drawBackIcon(int16_t cx, int16_t cy) {
    const uint16_t col = COL_MUTED;
    tft->drawLine(cx + 4, cy - 5, cx - 3, cy, col);
    tft->drawLine(cx - 3, cy, cx + 4, cy + 5, col);
    tft->drawFastHLine(cx - 3, cy, 13, col);
}

// ── Screen 0: Dashboard ───────────────────────────────────
static void drawDashboardHeader() {
    tft->fillRect(0, 0, 240, 28, COL_SURFACE);

    drawWifiIcon(158, 8, wifi::isConnected());
    drawGatewayIcon(174, 8, ecowitt::ecowittOk());

    char timeBuf[6] = "--:--";
    struct tm t;
    if (getLocalTime(&t, 50)) {
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", &t);
    }

    if (_smoothFonts) drawHeaderTextSmooth("BEWAESSERUNG", timeBuf);
    else              drawHeaderTextFallback("BEWAESSERUNG", timeBuf);
    drawHLine(28, COL_ACCENT);
}

static void drawDashboard() {
    tft->fillScreen(COL_BG);

    // Header
    drawDashboardHeader();

    uint8_t visibleZones = displayZoneCount();
    for (uint8_t zi = 0; zi < visibleZones; zi++) {
        const ecowitt::SoilData* sd = soilForChannel(zoneSensorChannel(zi));
        bool   open = valve::isOpen(zoneIdAt(zi));
        const int16_t rowY = 32 + zi * 43;
        const int16_t rowH = 43;
        String zName = zoneLabel(zi);
        bool active = zoneActiveAt(zi);

        // Zone-Name + Status
        const char* badge = open ? "OFFEN" : (active ? "bereit" : "inaktiv");
        uint16_t bc = open ? COL_ACCENT : (active ? COL_TEXT2 : COL_RED);
        if (_smoothFonts) {
            tft->loadFont("InterSemi12", SPIFFS);
            tft->setTextDatum(TL_DATUM);
            tft->setTextColor(open ? COL_ACCENT : (active ? COL_TEXT : COL_TEXT2), COL_BG, true);
            tft->drawString(zName, 10, rowY + 5);
            tft->setTextDatum(TR_DATUM);
            tft->setTextColor(bc, COL_BG, true);
            tft->drawString(badge, 228, rowY + 5);
            tft->unloadFont();
            tft->setTextDatum(TL_DATUM);
        } else {
            tft->setTextColor(open ? COL_ACCENT : (active ? COL_TEXT : COL_TEXT2), COL_BG);
            tft->setTextSize(1);
            tft->setCursor(10, rowY + 4);
            tft->print(zName);

            tft->setTextColor(bc, COL_BG);
            tft->setCursor(175, rowY + 4);
            tft->print(badge);
        }

        // Sensorwerte
        String sensorLine;
        if (sd && sd->valid) {
            char buf[32];
            if (!isnan(sd->temp) && !isnan(sd->ec)) {
                snprintf(buf, sizeof(buf), "%.0f%%  %.1fC  %.0fuS", sd->moisture, sd->temp, sd->ec);
            } else if (!isnan(sd->temp)) {
                snprintf(buf, sizeof(buf), "%.0f%%  %.1fC", sd->moisture, sd->temp);
            } else {
                snprintf(buf, sizeof(buf), "%.0f%%", sd->moisture);
            }
            sensorLine = buf;
        } else {
            sensorLine = "-- Kein Sensor --";
        }

        String scheduleLine = nextScheduleLine(zoneIdAt(zi));
        String skipLine = currentSkipReason(zi);
        bool willRun = skipLine.startsWith("Plan");

        if (_smoothFonts) {
            tft->loadFont("InterReg11", SPIFFS);
            tft->setTextDatum(TL_DATUM);
            tft->setTextColor(COL_MUTED, COL_BG, true);
            tft->drawString(fitTextWidth(sensorLine, 95), 10, rowY + 21);
            tft->setTextColor(willRun ? COL_ACCENT : COL_TEXT, COL_BG, true);
            tft->drawString(fitTextWidth(scheduleLine, 112), 118, rowY + 21);
            tft->setTextColor(skipLine.startsWith("Skip") ? COL_AMBER : COL_MUTED, COL_BG, true);
            tft->drawString(fitTextWidth(skipLine, 220), 10, rowY + 32);
            tft->unloadFont();
        } else {
            tft->setTextColor(COL_MUTED, COL_BG);
            tft->setCursor(10, rowY + 18);
            tft->print(sensorLine);
            tft->setTextColor(willRun ? COL_ACCENT : COL_TEXT, COL_BG);
            tft->setCursor(118, rowY + 18);
            tft->print(scheduleLine);
            tft->setTextColor(skipLine.startsWith("Skip") ? COL_AMBER : COL_MUTED, COL_BG);
            tft->setCursor(10, rowY + 30);
            tft->print(skipLine);
        }

        drawHLine(rowY + rowH - 1, COL_BORDER);
    }

    drawNavBar();
}

// ── Screen 1: Sensor-Livewerte ────────────────────────────
static String sensorValueLine(const ecowitt::SoilData* sd) {
    if (!sd || !sd->valid) return "-- Kein Sensor --";
    char buf[40];
    if (!isnan(sd->temp) && !isnan(sd->ec)) {
        snprintf(buf, sizeof(buf), "%.0f%%  %.1fC  %.0fuS", sd->moisture, sd->temp, sd->ec);
    } else if (!isnan(sd->temp)) {
        snprintf(buf, sizeof(buf), "%.0f%%  %.1fC", sd->moisture, sd->temp);
    } else {
        snprintf(buf, sizeof(buf), "%.0f%%", sd->moisture);
    }
    return String(buf);
}

static String sensorAgeLine(const ecowitt::SoilData* sd) {
    if (!sd || !sd->valid || sd->ts == 0) return "keine frischen Daten";
    unsigned long ageSec = (millis() - sd->ts) / 1000UL;
    char buf[28];
    if (ageSec < 60) snprintf(buf, sizeof(buf), "vor %us", (unsigned)ageSec);
    else             snprintf(buf, sizeof(buf), "vor %umin", (unsigned)(ageSec / 60));
    return String(buf);
}

static void drawSensors() {
    tft->fillScreen(COL_BG);
    tft->setTextSize(1);

    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_ACCENT, COL_BG, true);
        tft->drawString("SENSOR LIVE", 8, 5);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_ACCENT, COL_BG);
        tft->setTextSize(1);
        tft->setCursor(8, 5);
        tft->print("SENSOR LIVE");
    }
    drawHLine(21, COL_ACCENT);
    drawGearIcon(218, 9);

    uint8_t visibleZones = displayZoneCount();
    for (uint8_t zi = 0; zi < visibleZones; zi++) {
        uint8_t channel = zoneSensorChannel(zi);
        const ecowitt::SoilData* sd = soilForChannel(channel);
        const int16_t rowY = 32 + zi * 40;
        String name = zoneLabel(zi, "Z");
        String values = sensorValueLine(sd);
        String age = sensorAgeLine(sd);
        bool shared = false;
        for (uint8_t other = 0; other < visibleZones; other++) {
            if (other != zi && zoneSensorChannel(other) == channel) {
                shared = true;
                break;
            }
        }

        if (_smoothFonts) {
            tft->loadFont("InterSemi12", SPIFFS);
            tft->setTextDatum(TL_DATUM);
            tft->setTextColor(COL_TEXT, COL_BG, true);
            tft->drawString(fitTextWidth(name, 122), 10, rowY);
            tft->setTextDatum(TR_DATUM);
            tft->setTextColor(shared ? COL_AMBER : COL_MUTED, COL_BG, true);
            char chBuf[18];
            snprintf(chBuf, sizeof(chBuf), shared ? "Ch %u geteilt" : "Ch %u", channel);
            tft->drawString(chBuf, 230, rowY);
            tft->unloadFont();

            tft->loadFont("InterReg11", SPIFFS);
            tft->setTextDatum(TL_DATUM);
            tft->setTextColor(sd && sd->valid ? COL_ACCENT : COL_AMBER, COL_BG, true);
            tft->drawString(fitTextWidth(values, 140), 10, rowY + 16);
            tft->setTextDatum(TR_DATUM);
            tft->setTextColor(COL_MUTED, COL_BG, true);
            tft->drawString(age, 230, rowY + 16);
            tft->unloadFont();
            tft->setTextDatum(TL_DATUM);
        } else {
            tft->setTextColor(COL_TEXT, COL_BG);
            tft->setCursor(10, rowY);
            tft->print(name);
            tft->setTextColor(shared ? COL_AMBER : COL_MUTED, COL_BG);
            tft->setCursor(172, rowY);
            tft->printf(shared ? "Ch %u get." : "Ch %u", channel);
            tft->setTextColor(sd && sd->valid ? COL_ACCENT : COL_AMBER, COL_BG);
            tft->setCursor(10, rowY + 15);
            tft->print(values);
            tft->setTextColor(COL_MUTED, COL_BG);
            tft->setCursor(172, rowY + 15);
            tft->print(age);
        }
        drawHLine(rowY + 34, COL_BORDER);
    }

    if (visibleZones == 0) {
        tft->setTextColor(COL_MUTED, COL_BG);
        tft->setCursor(10, 56);
        tft->print("Keine Zonen konfiguriert");
    }

    drawNavBar();
}

// ── Screen 3: System ──────────────────────────────────────
static void drawSystem() {
    tft->fillScreen(COL_BG);
    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_ACCENT, COL_BG, true);
        tft->drawString("SYSTEM", 8, 5);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_ACCENT, COL_BG);
        tft->setTextSize(1);
        tft->setCursor(8, 5);
        tft->print("SYSTEM");
    }
    drawHLine(21, COL_ACCENT);

    const char* items[] = {
        "Diagnose",
        "Eventlog",
        "OTA Update",
        "Ausgaenge",
        "Touch-Kalibrierung"
    };

    if (_smoothFonts) {
        tft->loadFont("InterReg11", SPIFFS);
        tft->setTextDatum(ML_DATUM);
    }
    for (uint8_t i = 0; i < 5; i++) {
        int16_t y = 38 + i * 42;
        tft->drawRect(8, y, 224, 32, COL_BORDER);
        if (_smoothFonts) {
            tft->setTextColor(COL_TEXT, COL_BG, true);
            tft->drawString(items[i], 18, y + 16);
            tft->setTextColor(COL_MUTED, COL_BG, true);
            tft->drawString(">", 220, y + 16);
        } else {
            tft->setTextColor(COL_TEXT, COL_BG);
            tft->setCursor(18, y + 12);
            tft->print(items[i]);
            tft->setTextColor(COL_MUTED, COL_BG);
            tft->setCursor(220, y + 12);
            tft->print(">");
        }
    }
    if (_smoothFonts) {
        tft->unloadFont();
        tft->setTextDatum(TL_DATUM);
    }

    drawNavBar();
}

// ── Screen 4: Diagnose ────────────────────────────────────
static void drawDiagRow(uint8_t row, const char* label, const String& value, uint16_t valueColor = COL_TEXT) {
    const int16_t y = 34 + row * 22;
    if (_smoothFonts) {
        tft->loadFont("InterReg11", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_MUTED, COL_BG, true);
        tft->drawString(label, 8, y);
        tft->setTextDatum(TR_DATUM);
        tft->setTextColor(valueColor, COL_BG, true);
        tft->drawString(value, 232, y);
        tft->unloadFont();
        tft->setTextDatum(TL_DATUM);
    } else {
        tft->setTextColor(COL_MUTED, COL_BG);
        tft->setCursor(8, y);
        tft->print(label);
        tft->setTextColor(valueColor, COL_BG);
        tft->setCursor(126, y);
        tft->print(value);
    }
}

static String formatUptime(uint32_t sec) {
    uint16_t days = sec / 86400UL;
    sec %= 86400UL;
    uint8_t hours = sec / 3600UL;
    sec %= 3600UL;
    uint8_t mins = sec / 60UL;
    char buf[18];
    if (days > 0) snprintf(buf, sizeof(buf), "%ud %02u:%02u", days, hours, mins);
    else          snprintf(buf, sizeof(buf), "%02u:%02u", hours, mins);
    return String(buf);
}

static String sensorAgeText() {
    unsigned long newest = 0;
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < ECOWITT_MAX_SOIL_CHANNELS; i++) {
        const ecowitt::SoilData& sd = ecowitt::soil[i];
        if (!sd.valid || sd.ts == 0) continue;
        validCount++;
        if (sd.ts > newest) newest = sd.ts;
    }
    if (validCount == 0 || newest == 0) return "keine Daten";
    unsigned long ageSec = (millis() - newest) / 1000UL;
    char buf[24];
    if (ageSec < 60) snprintf(buf, sizeof(buf), "%us / %u Sensor", (unsigned)ageSec, validCount);
    else             snprintf(buf, sizeof(buf), "%umin / %u Sensor", (unsigned)(ageSec / 60), validCount);
    return String(buf);
}

static String eventDateTimeText(const char* createdAt) {
    if (createdAt && strlen(createdAt) >= 16) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%c%c.%c%c %c%c:%c%c",
                 createdAt[8], createdAt[9],
                 createdAt[5], createdAt[6],
                 createdAt[11], createdAt[12],
                 createdAt[14], createdAt[15]);
        return String(buf);
    }
    return "--.-- --:--";
}

static const char* eventActionText(const char* action) {
    if (strcmp(action, "open") == 0) return "AUF";
    if (strcmp(action, "close") == 0) return "ZU";
    if (strcmp(action, "skip") == 0) return "SKIP";
    if (strcmp(action, "reset") == 0) return "RESET";
    return action && action[0] ? action : "-";
}

static void drawEventLog() {
    tft->fillScreen(COL_BG);
    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_ACCENT, COL_BG, true);
        tft->drawString("EVENTLOG", 8, 5);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_ACCENT, COL_BG);
        tft->setTextSize(1);
        tft->setCursor(8, 5);
        tft->print("EVENTLOG");
    }
    drawHLine(21, COL_ACCENT);
    drawBackIcon(218, 12);

    uint8_t cnt = events::recentCount();
    if (cnt == 0) {
        if (_smoothFonts) {
            tft->loadFont("InterReg11", SPIFFS);
            tft->setTextDatum(TL_DATUM);
            tft->setTextColor(COL_MUTED, COL_BG, true);
            tft->drawString("Noch keine Events seit Neustart.", 8, 48);
            tft->unloadFont();
        } else {
            tft->setTextColor(COL_MUTED, COL_BG);
            tft->setCursor(8, 48);
            tft->print("Noch keine Events seit Neustart.");
        }
        drawNavBar();
        return;
    }

    if (_smoothFonts) {
        tft->loadFont("InterReg11", SPIFFS);
        tft->setTextDatum(TL_DATUM);
    }

    uint8_t rows = min<uint8_t>(cnt, 14);
    for (uint8_t i = 0; i < rows; i++) {
        events::RecentEvent e;
        if (!events::recentAt(i, e)) continue;

        int16_t y = 34 + i * 18;
        String line = eventDateTimeText(e.created_at) + "  ";
        if (e.zone_id > 0) line += String("V") + String(e.zone_id);
        else               line += "SYS";
        line += " ";
        line += eventActionText(e.action);
        String detail = e.detail[0] ? String(e.detail) : String(e.reason);
        if (detail.length() > 0) {
            line += " ";
            line += tftText(detail.c_str());
        }
        line = fitTextWidth(line, 224);

        if (_smoothFonts) {
            tft->setTextColor(COL_TEXT, COL_BG, true);
            tft->drawString(line, 8, y);
        } else {
            tft->setTextColor(COL_TEXT, COL_BG);
            tft->setCursor(8, y);
            tft->print(line);
        }
        drawDashedHLine(y + 14, COL_BORDER);
    }

    if (_smoothFonts) {
        tft->unloadFont();
        tft->setTextDatum(TL_DATUM);
    }
    drawNavBar();
}

static void drawOtaPage() {
    tft->fillScreen(COL_BG);
    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_ACCENT, COL_BG, true);
        tft->drawString("OTA UPDATE", 8, 5);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_ACCENT, COL_BG);
        tft->setCursor(8, 5);
        tft->print("OTA UPDATE");
    }
    drawHLine(21, COL_ACCENT);
    drawBackIcon(218, 12);

    const bool wifiOk = wifi::isConnected();
    const bool otaOn = ota::active();
    const uint16_t stateCol = otaOn ? COL_ACCENT : (wifiOk ? COL_MUTED : COL_RED);

    if (_smoothFonts) {
        tft->loadFont("InterReg11", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_MUTED, COL_BG, true);
        tft->drawString("Status", 8, 42);
        tft->setTextColor(stateCol, COL_BG, true);
        tft->drawString(ota::statusText(), 76, 42);

        tft->setTextColor(COL_MUTED, COL_BG, true);
        tft->drawString("WiFi", 8, 64);
        tft->setTextColor(wifiOk ? COL_ACCENT : COL_RED, COL_BG, true);
        tft->drawString(wifiOk ? "verbunden" : "getrennt", 76, 64);

        tft->setTextColor(COL_MUTED, COL_BG, true);
        tft->drawString("Host", 8, 86);
        tft->setTextColor(COL_TEXT, COL_BG, true);
        tft->drawString(ota::hostname(), 76, 86);

        tft->setTextColor(COL_MUTED, COL_BG, true);
        tft->drawString("IP", 8, 108);
        tft->setTextColor(COL_TEXT, COL_BG, true);
        tft->drawString(wifiOk ? WiFi.localIP().toString() : "-", 76, 108);

        tft->setTextColor(COL_MUTED, COL_BG, true);
        tft->drawString("Fenster", 8, 130);
        tft->setTextColor(otaOn ? COL_ACCENT : COL_MUTED, COL_BG, true);
        String left = otaOn ? String(ota::secondsLeft()) + "s offen" : "geschlossen";
        tft->drawString(left, 76, 130);

        tft->setTextColor(COL_MUTED, COL_BG, true);
        tft->drawString("Fortschritt", 8, 152);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_MUTED, COL_BG);
        tft->setCursor(8, 42);
        tft->print("Status");
        tft->setTextColor(stateCol, COL_BG);
        tft->setCursor(76, 42);
        tft->print(ota::statusText());
    }

    tft->drawRect(8, 170, 224, 14, COL_BORDER);
    uint8_t p = ota::progress();
    if (p > 0) tft->fillRect(10, 172, (220 * p) / 100, 10, COL_ACCENT);

    const int16_t bx = 20;
    const int16_t by = 214;
    const int16_t bw = 200;
    const int16_t bh = 38;
    uint16_t buttonCol = otaOn ? COL_AMBER : (wifiOk ? COL_ACCENT : COL_BORDER);
    tft->drawRect(bx, by, bw, bh, buttonCol);
    const char* label = otaOn ? "OTA STOPPEN" : "OTA STARTEN";

    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(buttonCol, COL_BG, true);
        tft->drawString(label, bx + bw / 2, by + bh / 2 + 1);
        tft->unloadFont();
        tft->setTextDatum(TL_DATUM);
    } else {
        tft->setTextColor(buttonCol, COL_BG);
        tft->setCursor(bx + 54, by + 15);
        tft->print(label);
    }

    drawNavBar();
}

static void drawOutputsPage() {
    tft->fillScreen(COL_BG);
    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_ACCENT, COL_BG, true);
        tft->drawString("AUSGAENGE", 8, 5);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_ACCENT, COL_BG);
        tft->setCursor(8, 5);
        tft->print("AUSGAENGE");
    }
    drawHLine(21, COL_ACCENT);
    drawBackIcon(218, 12);

    if (_smoothFonts) {
        tft->loadFont("InterReg11", SPIFFS);
        tft->setTextDatum(TL_DATUM);
    }

    for (uint8_t i = 0; i < displayZoneCount(); i++) {
        int16_t y = 36 + i * 28;
        uint8_t zone = zoneIdAt(i);
        bool open = valve::isOpen(zone);
        uint16_t col = open ? COL_ACCENT : COL_MUTED;
        tft->drawRect(8, y, 224, 22, open ? COL_ACCENT : COL_BORDER);
        tft->fillCircle(20, y + 11, 4, col);

        char left[34];
        snprintf(left, sizeof(left), "IN%u  GPIO%u", i + 1, relayGpioForIndex(i));
        String right = zoneLabel(i, "Z");
        right += open ? "  EIN" : "  AUS";

        if (_smoothFonts) {
            tft->setTextColor(COL_MUTED, COL_BG, true);
            tft->drawString(left, 32, y + 6);
            tft->setTextDatum(TR_DATUM);
            tft->setTextColor(open ? COL_ACCENT : COL_TEXT, COL_BG, true);
            tft->drawString(fitTextWidth(right, 108), 224, y + 6);
            tft->setTextDatum(TL_DATUM);
        } else {
            tft->setTextColor(COL_MUTED, COL_BG);
            tft->setCursor(32, y + 7);
            tft->print(left);
            tft->setTextColor(open ? COL_ACCENT : COL_TEXT, COL_BG);
            tft->setCursor(126, y + 7);
            tft->print(fitTextWidth(right, 100));
        }
    }

    if (_smoothFonts) {
        tft->unloadFont();
        tft->setTextDatum(TL_DATUM);
    }

    const int16_t resetX = 20;
    const int16_t resetY = 228;
    const int16_t resetW = 200;
    const int16_t resetH = 34;
    tft->drawRect(resetX, resetY, resetW, resetH, COL_AMBER);
    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG, true);
        tft->drawString("ALLE AUS", resetX + resetW / 2, resetY + resetH / 2 + 1);
        tft->unloadFont();
        tft->setTextDatum(TL_DATUM);
    } else {
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->setCursor(resetX + 76, resetY + 13);
        tft->print("ALLE AUS");
    }

    drawNavBar();
}

static void drawTouchCalibrationPage() {
    tft->fillScreen(COL_BG);
    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_ACCENT, COL_BG, true);
        tft->drawString("TOUCH-KALIBRIERUNG", 8, 5);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_ACCENT, COL_BG);
        tft->setCursor(8, 5);
        tft->print("TOUCH-KALIBRIERUNG");
    }
    drawHLine(21, COL_ACCENT);
    drawBackIcon(218, 12);

    if (_smoothFonts) {
        tft->loadFont("InterReg11", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_MUTED, COL_BG, true);
        tft->drawString("Startet die TFT_eSPI Kalibrierung.", 8, 48);
        tft->drawString("Danach die Markierungen genau", 8, 66);
        tft->drawString("in der Mitte antippen.", 8, 84);
        tft->setTextColor(COL_AMBER, COL_BG, true);
        tft->drawString("Nur starten, wenn Touch erreichbar ist.", 8, 118);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_MUTED, COL_BG);
        tft->setCursor(8, 48);
        tft->print("Markierungen genau antippen.");
    }

    const int16_t bx = 20;
    const int16_t by = 188;
    const int16_t bw = 200;
    const int16_t bh = 42;
    tft->drawRect(bx, by, bw, bh, COL_ACCENT);
    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_ACCENT, COL_BG, true);
        tft->drawString("KALIBRIEREN", bx + bw / 2, by + bh / 2 + 1);
        tft->unloadFont();
        tft->setTextDatum(TL_DATUM);
    } else {
        tft->setTextColor(COL_ACCENT, COL_BG);
        tft->setCursor(bx + 64, by + 17);
        tft->print("KALIBRIEREN");
    }

    drawNavBar();
}

static void drawDiagnostics() {
    tft->fillScreen(COL_BG);
    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_ACCENT, COL_BG, true);
        tft->drawString("DIAGNOSE", 8, 5);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_ACCENT, COL_BG);
        tft->setTextSize(1);
        tft->setCursor(8, 5);
        tft->print("DIAGNOSE");
    }
    drawHLine(21, COL_ACCENT);
    drawBackIcon(218, 12);

    bool wifiOk = wifi::isConnected();
    bool gwOk = ecowitt::ecowittOk();
    bool rtcOk = rtc::available();
    bool ntpOk = wifi::ntpSynced();

    drawDiagRow(0, "WiFi", wifiOk ? "verbunden" : "getrennt", wifiOk ? COL_ACCENT : COL_RED);
    drawDiagRow(1, "IP", wifiOk ? WiFi.localIP().toString() : "-", COL_TEXT);
    drawDiagRow(2, "RSSI", wifiOk ? String(wifi::rssi()) + " dBm" : "-", wifiOk ? COL_TEXT : COL_MUTED);
    drawDiagRow(3, "GW1200", gwOk ? "erreichbar" : "offline", gwOk ? COL_ACCENT : COL_RED);
    drawDiagRow(4, "Sensoren", sensorAgeText(), COL_TEXT);
    drawDiagRow(5, "RTC", rtcOk ? "ok" : "fehlt", rtcOk ? COL_ACCENT : COL_RED);
    drawDiagRow(6, "NTP", ntpOk ? "sync" : "offen", ntpOk ? COL_ACCENT : COL_AMBER);
    drawDiagRow(7, "Config", String("v") + String(cfg::version), COL_TEXT);
    drawDiagRow(8, "Heap", String(ESP.getFreeHeap() / 1024) + " KB", COL_TEXT);
    drawDiagRow(9, "PSRAM", String(ESP.getFreePsram() / 1024) + " KB", COL_TEXT);
    drawDiagRow(10, "Uptime", formatUptime(wifi::uptimeSec()), COL_TEXT);

    drawNavBar();
}

// ── Screen 2: Manuell ─────────────────────────────────────
static void drawManual() {
    tft->fillScreen(COL_BG);
    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_ACCENT, COL_BG, true);
        tft->drawString("VENTILSTEUERUNG", 8, 5);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_ACCENT, COL_BG);
        tft->setTextSize(1);
        tft->setCursor(8, 5);
        tft->print("VENTILSTEUERUNG");
    }
    drawHLine(21, COL_ACCENT);
    drawGearIcon(218, 9);

    uint8_t visibleZones = displayZoneCount();

    // Ventil-Buttons (2x3)
    for (uint8_t i = 0; i < visibleZones; i++) {
        int16_t x = (i % 2) * 122 + 4;
        int16_t y = (i / 2) * 57 + 22;
        uint8_t zoneId = zoneIdAt(i);
        bool open = valve::isOpen(zoneId);
        uint16_t bc = open ? COL_ACCENT : COL_BORDER;
        tft->drawRect(x, y, 112, 51, bc);
        if (open) tft->drawRect(x + 1, y + 1, 110, 49, bc);
        char valveBuf[4];
        snprintf(valveBuf, sizeof(valveBuf), "V%u", i + 1);

        if (_smoothFonts) {
            tft->loadFont("InterSemi12", SPIFFS);
            tft->setTextDatum(TL_DATUM);
            tft->setTextColor(open ? COL_ACCENT : COL_TEXT, COL_BG, true);
            tft->drawString(fitTextWidth(zoneLabel(i), 100), x + 6, y + 6);
            tft->unloadFont();
        } else {
            tft->setTextColor(open ? COL_ACCENT : COL_TEXT, COL_BG);
            tft->setCursor(x + 6, y + 10);
            tft->print(zoneLabel(i));
        }

        const ecowitt::SoilData* sd = soilForChannel(zoneSensorChannel(i));
        char moistureBuf[12] = "--";
        if (sd && sd->valid) {
            snprintf(moistureBuf, sizeof(moistureBuf), "%.0f%%", sd->moisture);
        }
        if (_smoothFonts) {
            tft->loadFont("InterReg11", SPIFFS);
            tft->setTextDatum(TL_DATUM);
            tft->setTextColor(COL_MUTED, COL_BG, true);
            tft->drawString(moistureBuf, x + 6, y + 23);

            uint16_t sc = open ? COL_ACCENT : COL_MUTED;
            tft->setTextColor(sc, COL_BG, true);
            if (open) tft->fillCircle(x + 10, y + 40, 3, sc);
            else      tft->drawCircle(x + 10, y + 40, 3, sc);
            tft->drawString(open ? "OFFEN" : "AUS", x + 18, y + 34);

            tft->setTextDatum(BR_DATUM);
            tft->setTextColor(open ? COL_ACCENT : COL_MUTED, COL_BG, true);
            tft->drawString(valveBuf, x + 106, y + 45);
            tft->unloadFont();
            tft->setTextDatum(TL_DATUM);
        } else {
            tft->setTextColor(COL_MUTED, COL_BG);
            tft->setCursor(x + 6, y + 22);
            tft->print(moistureBuf);

            tft->setTextColor(open ? COL_ACCENT : COL_MUTED, COL_BG);
            tft->setCursor(x + 6, y + 36);
            tft->print(open ? "* OFFEN" : "o AUS");
            tft->setCursor(x + 94, y + 36);
            tft->print(valveBuf);
        }
    }

    // Dauer-Buttons
    const uint8_t durs[] = {15, 30, 60, 90};
    if (_smoothFonts) {
        tft->loadFont("InterReg11", SPIFFS);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(COL_MUTED, COL_BG, true);
        tft->drawString("Dauer:", 8, 202);
        tft->unloadFont();
    } else {
        tft->setTextColor(COL_MUTED, COL_BG);
        tft->setCursor(8, 203);
        tft->print("Dauer:");
    }
    for (uint8_t i = 0; i < 4; i++) {
        int16_t x = i * 56 + 8;
        bool sel = (durs[i] == _manDur);
        tft->drawRect(x, 214, 50, 22, sel ? COL_ACCENT : COL_BORDER);
        char durBuf[6];
        snprintf(durBuf, sizeof(durBuf), "%um", durs[i]);
        if (_smoothFonts) {
            tft->loadFont("InterReg11", SPIFFS);
            tft->setTextDatum(MC_DATUM);
            tft->setTextColor(sel ? COL_ACCENT : COL_MUTED, COL_BG, true);
            tft->drawString(durBuf, x + 25, 225);
            tft->unloadFont();
            tft->setTextDatum(TL_DATUM);
        } else {
            tft->setTextColor(sel ? COL_ACCENT : COL_MUTED, COL_BG);
            tft->setCursor(x + 6, 220);
            tft->print(durBuf);
        }
    }

    // STOP ALL Button
    const int16_t stopX = 4;
    const int16_t stopY = 246;
    const int16_t stopW = 232;
    const int16_t stopH = 30;
    tft->drawRect(stopX, stopY, stopW, stopH, COL_RED);
    const char* stopLabel = "ALLE STOP";
    if (_smoothFonts) {
        tft->loadFont("InterSemi12", SPIFFS);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_RED, COL_BG, true);
        tft->drawString(stopLabel, stopX + stopW / 2, stopY + stopH / 2 + 1);
        tft->unloadFont();
        tft->setTextDatum(TL_DATUM);
    } else {
        tft->setTextColor(COL_RED, COL_BG);
        tft->setCursor(stopX + (stopW - (int16_t)strlen(stopLabel) * 6) / 2, stopY + (stopH - 8) / 2);
        tft->print(stopLabel);
    }

    drawNavBar();
}

// ── Touch-Handler ─────────────────────────────────────────
static void handleManualTouch(uint16_t tx, uint16_t ty) {
    // Ventil-Buttons (2x3 Grid)
    for (uint8_t i = 0; i < displayZoneCount(); i++) {
        int16_t bx = (i % 2) * 122 + 4;
        int16_t by = (i / 2) * 57 + 22;
        if (tx >= bx && tx < bx + 112 && ty >= by && ty < by + 51) {
            uint8_t zone = zoneIdAt(i);
            if (valve::isOpen(zone)) {
                valve::close(zone);
                scheduler::clearManualRun(zone);
                events::log(zone, "close", "manual", "Touch", 0, NAN, NAN, NAN, NAN);
            } else {
                uint32_t durMs = (uint32_t)_manDur * 60000UL;
                if (valve::open(zone, durMs)) {
                    char det[32];
                    snprintf(det, sizeof(det), "Touch %umin", _manDur);
                    events::log(zone, "open", "manual", det, 0, NAN, NAN, NAN, NAN);
                    scheduler::trackManualOpen(zone, durMs);
                }
            }
            return;
        }
    }

    // Dauer-Buttons
    const uint8_t durs[] = {15, 30, 60, 90};
    for (uint8_t i = 0; i < 4; i++) {
        int16_t bx = i * 56 + 8;
        if (tx >= bx && tx < bx + 50 && ty >= 214 && ty < 236) {
            _manDur = durs[i];
            return;
        }
    }

    // STOP ALL
    if (tx >= 4 && tx < 236 && ty >= 246 && ty < 276) {
        tft->fillRect(4, 246, 232, 30, COL_RED);
        tft->setTextColor(COL_BG, COL_RED);
        const char* stoppingLabel = "STOPPE ALLE...";
        tft->setCursor(4 + (232 - (int16_t)strlen(stoppingLabel) * 6) / 2, 246 + (30 - 8) / 2);
        tft->print(stoppingLabel);
        valve::closeAll();
        scheduler::clearManualRuns();
        events::log(0, "close", "manual", "Touch STOP ALL", 0, NAN, NAN, NAN, NAN);
    }
}

// ── Initialisierung ───────────────────────────────────────
void display::init() {
    static TFT_eSPI _tftObj;
    tft = &_tftObj;

    Serial.println("Display init: pins CS=41 DC=42 RST=21 BL=47 SCK=40 MOSI=38 MISO=39");
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);

    tft->init();
    tft->setRotation(DISPLAY_ROTATION);
    tft->invertDisplay(false);
    blFull();
    initSmoothFonts();
    Serial.println("Display test: red");
    tft->fillScreen(TFT_RED);
    delay(700);
    Serial.println("Display test: green");
    tft->fillScreen(TFT_GREEN);
    delay(700);
    Serial.println("Display test: blue");
    tft->fillScreen(TFT_BLUE);
    delay(700);
    Serial.println("Display test: white");
    tft->fillScreen(TFT_WHITE);
    delay(700);
    tft->fillScreen(COL_BG);
    loadCalibration();
    drawDashboard();
}

void display::refresh() {
    // Backlight-Timeout
    if (!_dimmed && millis() - _lastTouchMs > DISPLAY_BACKLIGHT_DIM_MS) blDim();

    switch (_screen) {
        case 0: drawDashboard(); break;
        case 1: drawSensors();   break;
        case 2: drawManual();    break;
        case 3: drawSystem();    break;
        case 4: drawDiagnostics(); break;
        case 5: drawEventLog();  break;
        case 6: drawOtaPage();   break;
        case 7: drawOutputsPage(); break;
        case 8: drawTouchCalibrationPage(); break;
    }
}

void display::periodicRefresh() {
    // Backlight-Timeout ohne unnötiges Neuzeichnen auf statischen Seiten.
    if (!_dimmed && millis() - _lastTouchMs > DISPLAY_BACKLIGHT_DIM_MS) blDim();
    if (_screen == 0) drawDashboardHeader();
    if (_screen == 6) drawOtaPage();
}

void display::handleTouch() {
    static unsigned long lastHandledMs = 0;
    uint16_t tx, ty;
    if (!tft->getTouch(&tx, &ty)) return;
    if (millis() - lastHandledMs < 250) return;
    lastHandledMs = millis();

    if (!_touchNative) {
        uint16_t mappedX = (uint32_t)ty * tft->width() / tft->height();
        uint16_t mappedY = (uint32_t)tx * tft->height() / tft->width();
        tx = min<uint16_t>(mappedX, tft->width() - 1);
        ty = min<uint16_t>(mappedY, tft->height() - 1);
    }

    if (_dimmed) { blFull(); return; }  // nur aufwecken, keine Aktion
    _lastTouchMs = millis();

    // Zahnrad oben rechts auf Sensor und Manuell
    if ((_screen == 1 || _screen == 2) && tx >= 198 && tx < 236 && ty < 28) {
        _screen = 3;
        refresh();
        return;
    }

    // Zurueck-Pfeil oben rechts auf Unterseiten des Systembereichs
    if ((_screen == 4 || _screen == 5 || _screen == 6 || _screen == 7 || _screen == 8) && tx >= 198 && tx < 236 && ty < 28) {
        _screen = 3;
        refresh();
        return;
    }

    // System-Menü
    if (_screen == 3 && tx >= 8 && tx < 232) {
        for (uint8_t i = 0; i < 5; i++) {
            int16_t itemY = 38 + i * 42;
            if (ty >= itemY && ty < itemY + 32) {
                if (i == 0) {
                    _screen = 4;
                    refresh();
                } else if (i == 1) {
                    _screen = 5;
                    refresh();
                } else if (i == 2) {
                    _screen = 6;
                    refresh();
                } else if (i == 3) {
                    _screen = 7;
                    refresh();
                } else if (i == 4) {
                    _screen = 8;
                    refresh();
                }
                return;
            }
        }
    }

    if (_screen == 6 && tx >= 20 && tx < 220 && ty >= 214 && ty < 252) {
        if (ota::active()) ota::stop();
        else               ota::startWindow(OTA_ENABLE_WINDOW_MS);
        refresh();
        return;
    }

    if (_screen == 7) {
        if (tx >= 20 && tx < 220 && ty >= 228 && ty < 262) {
            valve::closeAll();
            scheduler::clearManualRuns();
            events::log(0, "close", "manual", "Ausgaenge ALLE AUS", 0, NAN, NAN, NAN, NAN);
            refresh();
            return;
        }
    }

    if (_screen == 8 && tx >= 20 && tx < 220 && ty >= 188 && ty < 230) {
        runTouchCalibration();
        _screen = 3;
        refresh();
        return;
    }

    // Nav-Bar (y 292–317)
    if (ty >= 292) {
        uint8_t newScreen = tx / 80;
        if (newScreen != _screen) { _screen = newScreen; refresh(); }
        return;
    }

    if (_screen == 2) {
        handleManualTouch(tx, ty);
        refresh();
    }
}
