#include <Arduino.h>
#include "config.h"
#include "watchdog.h"
#include "wifi_manager.h"
#include "valve_driver.h"
#include "config_sync.h"
#include "event_logger.h"
#include "ecowitt_client.h"
#include "scheduler.h"
#include "display.h"
#include "rtc_clock.h"
#include "ota_update.h"

#ifdef TOUCH_DIAG_ONLY
#include <TFT_eSPI.h>

static uint16_t touchReadRaw(uint8_t command) {
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TOUCH_CS, LOW);
    delayMicroseconds(5);

    for (int8_t bit = 7; bit >= 0; bit--) {
        digitalWrite(TFT_SCLK, LOW);
        digitalWrite(TFT_MOSI, (command >> bit) & 1);
        delayMicroseconds(6);
        digitalWrite(TFT_SCLK, HIGH);
        delayMicroseconds(6);
    }

    uint16_t value = 0;
    for (uint8_t i = 0; i < 16; i++) {
        digitalWrite(TFT_SCLK, LOW);
        delayMicroseconds(6);
        digitalWrite(TFT_SCLK, HIGH);
        value <<= 1;
        if (digitalRead(TFT_MISO)) value |= 1;
        delayMicroseconds(6);
    }

    digitalWrite(TOUCH_CS, HIGH);
    return (value >> 3) & 0x0FFF;
}

static void wiggleTouchCs() {
    Serial.println("Wiggle T_CS GPIO8");
    for (uint8_t i = 0; i < 4; i++) {
        digitalWrite(TOUCH_CS, LOW);
        delay(200);
        digitalWrite(TOUCH_CS, HIGH);
        delay(200);
    }
}

static void runTouchDiagnostic() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== TOUCH DIAGNOSTIC ONLY ===");
    Serial.println("Display pins: CS=41 DC=42 RST=21 BL=47 SCK=40 MOSI=38 MISO=39");
    Serial.println("Touch pins: T_CS=8 T_IRQ=9 T_CLK=40 T_DIN=38 T_DO=39");

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(TFT_SCLK, OUTPUT);
    pinMode(TFT_MOSI, OUTPUT);
    pinMode(TFT_MISO, INPUT_PULLUP);
    digitalWrite(TFT_SCLK, HIGH);
    digitalWrite(TFT_MOSI, LOW);
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);
    pinMode(9, INPUT_PULLUP);

    static TFT_eSPI tft;
    Serial.println("Initialising TFT with TOUCH_CS held HIGH");
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("Touch point test");
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.print("Tippe: Punkt = gelesene Koordinate");
    tft.drawRect(0, 0, 240, 320, TFT_DARKGREY);
    tft.drawLine(0, 160, 239, 160, TFT_DARKGREY);
    tft.drawLine(120, 0, 120, 319, TFT_DARKGREY);

    wiggleTouchCs();

    uint32_t lastPrint = 0;
    while (true) {
        uint16_t x = 0, y = 0;
        bool touched = tft.getTouch(&x, &y);
        if (touched) {
            uint16_t mappedX = (uint32_t)y * tft.width() / tft.height();
            uint16_t mappedY = (uint32_t)x * tft.height() / tft.width();
            x = min<uint16_t>(mappedX, tft.width() - 1);
            y = min<uint16_t>(mappedY, tft.height() - 1);
        }
        bool irqLow = digitalRead(9) == LOW;
        uint16_t rawX = touchReadRaw(0xD0);
        uint16_t rawY = touchReadRaw(0x90);
        uint16_t rawZ1 = touchReadRaw(0xB0);
        uint16_t rawZ2 = touchReadRaw(0xC0);

        if (touched || millis() - lastPrint > 1000) {
            Serial.printf("touch=%d irqLow=%d x=%u y=%u rawX=%u rawY=%u z1=%u z2=%u\n",
                          touched, irqLow, x, y, rawX, rawY, rawZ1, rawZ2);
            tft.fillRect(0, 55, 240, 35, TFT_BLACK);
            tft.setCursor(10, 75);
            tft.printf("x=%u y=%u %s", x, y, touched ? "DOWN" : "up");
            if (touched) {
                tft.fillCircle(x, y, 4, TFT_RED);
                tft.drawCircle(x, y, 7, TFT_WHITE);
            }
            lastPrint = millis();
        }
        delay(50);
    }
}
#endif

#ifdef TFT_DIAG_ONLY
#include <TFT_eSPI.h>
#include <SPI.h>

#define TFT_DIAG_BITBANG_ONLY 1

#if TFT_DIAG_BITBANG_ONLY
static void bbDelay() {
    delayMicroseconds(1);
}

static void bbWrite8(uint8_t data) {
    for (int8_t bit = 7; bit >= 0; bit--) {
        digitalWrite(TFT_SCLK, LOW);
        digitalWrite(TFT_MOSI, (data >> bit) & 1);
        bbDelay();
        digitalWrite(TFT_SCLK, HIGH);
        bbDelay();
    }
}

static void bbCommand(uint8_t cmd) {
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, LOW);
    bbWrite8(cmd);
    digitalWrite(TFT_CS, HIGH);
}

static void bbData(uint8_t data) {
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, HIGH);
    bbWrite8(data);
    digitalWrite(TFT_CS, HIGH);
}

static void bbData16(uint16_t data) {
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, HIGH);
    bbWrite8(data >> 8);
    bbWrite8(data & 0xFF);
    digitalWrite(TFT_CS, HIGH);
}

static void bbAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    bbCommand(0x2A);
    bbData16(x0);
    bbData16(x1);
    bbCommand(0x2B);
    bbData16(y0);
    bbData16(y1);
    bbCommand(0x2C);
}

static void bbFill(uint16_t color) {
    bbAddrWindow(0, 0, 239, 319);
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, HIGH);
    for (uint32_t i = 0; i < 240UL * 320UL; i++) {
        bbWrite8(color >> 8);
        bbWrite8(color & 0xFF);
    }
    digitalWrite(TFT_CS, HIGH);
}

static void bbResetPins() {
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_RST, OUTPUT);
    pinMode(TFT_MOSI, OUTPUT);
    pinMode(TFT_SCLK, OUTPUT);
    pinMode(TFT_BL, OUTPUT);
    pinMode(TOUCH_CS, OUTPUT);

    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TOUCH_CS, HIGH);
    digitalWrite(TFT_SCLK, HIGH);
    digitalWrite(TFT_MOSI, LOW);
    digitalWrite(TFT_BL, HIGH);

    Serial.println("Bitbang reset");
    digitalWrite(TFT_RST, HIGH);
    delay(20);
    digitalWrite(TFT_RST, LOW);
    delay(80);
    digitalWrite(TFT_RST, HIGH);
    delay(150);
}

static void bbCommonWake() {
    bbCommand(0x01);
    delay(150);
    bbCommand(0x11);
    delay(150);
    bbCommand(0x29);
    delay(50);
}

static void bbInitIli9341() {
    bbResetPins();
    Serial.println("Bitbang ILI9341 init");
    bbCommand(0x01);
    delay(150);
    bbCommand(0x28);

    bbCommand(0xCF); bbData(0x00); bbData(0xC1); bbData(0x30);
    bbCommand(0xED); bbData(0x64); bbData(0x03); bbData(0x12); bbData(0x81);
    bbCommand(0xE8); bbData(0x85); bbData(0x00); bbData(0x78);
    bbCommand(0xCB); bbData(0x39); bbData(0x2C); bbData(0x00); bbData(0x34); bbData(0x02);
    bbCommand(0xF7); bbData(0x20);
    bbCommand(0xEA); bbData(0x00); bbData(0x00);
    bbCommand(0xC0); bbData(0x23);
    bbCommand(0xC1); bbData(0x10);
    bbCommand(0xC5); bbData(0x3E); bbData(0x28);
    bbCommand(0xC7); bbData(0x86);
    bbCommand(0x36); bbData(0x48);
    bbCommand(0x3A); bbData(0x55);
    bbCommand(0xB1); bbData(0x00); bbData(0x18);
    bbCommand(0xB6); bbData(0x08); bbData(0x82); bbData(0x27);
    bbCommand(0xF2); bbData(0x00);
    bbCommand(0x26); bbData(0x01);
    bbCommand(0x11);
    delay(150);
    bbCommand(0x29);
    delay(50);
}

static void bbInitIli9342() {
    bbResetPins();
    Serial.println("Bitbang ILI9342-like init");
    bbCommonWake();
    bbCommand(0x36); bbData(0x48);
    bbCommand(0x3A); bbData(0x55);
    bbCommand(0x2A); bbData16(0); bbData16(239);
    bbCommand(0x2B); bbData16(0); bbData16(319);
}

static void bbInitSt7789() {
    bbResetPins();
    Serial.println("Bitbang ST7789 init");
    bbCommand(0x01);
    delay(150);
    bbCommand(0x11);
    delay(150);
    bbCommand(0x36); bbData(0x00);
    bbCommand(0x3A); bbData(0x55);
    bbCommand(0xB2); bbData(0x0C); bbData(0x0C); bbData(0x00); bbData(0x33); bbData(0x33);
    bbCommand(0xB7); bbData(0x35);
    bbCommand(0xBB); bbData(0x19);
    bbCommand(0xC0); bbData(0x2C);
    bbCommand(0xC2); bbData(0x01);
    bbCommand(0xC3); bbData(0x12);
    bbCommand(0xC4); bbData(0x20);
    bbCommand(0xC6); bbData(0x0F);
    bbCommand(0xD0); bbData(0xA4); bbData(0xA1);
    bbCommand(0x21);
    bbCommand(0x29);
    delay(50);
}

static void bbInitSt7796() {
    bbResetPins();
    Serial.println("Bitbang ST7796-like init");
    bbCommand(0x01);
    delay(150);
    bbCommand(0x11);
    delay(150);
    bbCommand(0xF0); bbData(0xC3);
    bbCommand(0xF0); bbData(0x96);
    bbCommand(0x36); bbData(0x48);
    bbCommand(0x3A); bbData(0x55);
    bbCommand(0xB4); bbData(0x01);
    bbCommand(0xB6); bbData(0x80); bbData(0x02); bbData(0x3B);
    bbCommand(0xE8); bbData(0x40); bbData(0x8A); bbData(0x00); bbData(0x00); bbData(0x29); bbData(0x19); bbData(0xA5); bbData(0x33);
    bbCommand(0xC1); bbData(0x06);
    bbCommand(0xC2); bbData(0xA7);
    bbCommand(0xC5); bbData(0x18);
    bbCommand(0xF0); bbData(0x3C);
    bbCommand(0xF0); bbData(0x69);
    bbCommand(0x29);
    delay(50);
}

static void bbInitIli9488() {
    bbResetPins();
    Serial.println("Bitbang ILI9488-like init");
    bbCommand(0x01);
    delay(150);
    bbCommand(0x11);
    delay(150);
    bbCommand(0xE0); bbData(0x00); bbData(0x03); bbData(0x09); bbData(0x08); bbData(0x16); bbData(0x0A); bbData(0x3F); bbData(0x78); bbData(0x4C); bbData(0x09); bbData(0x0A); bbData(0x08); bbData(0x16); bbData(0x1A); bbData(0x0F);
    bbCommand(0xE1); bbData(0x00); bbData(0x16); bbData(0x19); bbData(0x03); bbData(0x0F); bbData(0x05); bbData(0x32); bbData(0x45); bbData(0x46); bbData(0x04); bbData(0x0E); bbData(0x0D); bbData(0x35); bbData(0x37); bbData(0x0F);
    bbCommand(0xC0); bbData(0x17); bbData(0x15);
    bbCommand(0xC1); bbData(0x41);
    bbCommand(0xC5); bbData(0x00); bbData(0x12); bbData(0x80);
    bbCommand(0x36); bbData(0x48);
    bbCommand(0x3A); bbData(0x55);
    bbCommand(0xB0); bbData(0x00);
    bbCommand(0xB1); bbData(0xA0);
    bbCommand(0xB4); bbData(0x02);
    bbCommand(0xB6); bbData(0x02); bbData(0x02);
    bbCommand(0xE9); bbData(0x00);
    bbCommand(0xF7); bbData(0xA9); bbData(0x51); bbData(0x2C); bbData(0x82);
    bbCommand(0x29);
    delay(50);
}

static void bbFillBars() {
    const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0xFFE0};
    for (uint8_t bar = 0; bar < 6; bar++) {
        uint16_t y0 = bar * 53;
        uint16_t y1 = (bar == 5) ? 319 : (y0 + 52);
        bbAddrWindow(0, y0, 239, y1);
        digitalWrite(TFT_CS, LOW);
        digitalWrite(TFT_DC, HIGH);
        uint32_t count = 240UL * (y1 - y0 + 1);
        for (uint32_t i = 0; i < count; i++) {
            bbWrite8(colors[bar] >> 8);
            bbWrite8(colors[bar] & 0xFF);
        }
        digitalWrite(TFT_CS, HIGH);
    }
}

static void runBitbangDiagnostic() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== TFT BITBANG DIAGNOSTIC ONLY ===");
    Serial.println("No TFT_eSPI, no hardware SPI. Pins: CS=41 DC=42 RST=21 SCK=40 MOSI=38 BL=47");

    void (*inits[])() = {bbInitIli9341, bbInitIli9342, bbInitSt7789, bbInitSt7796, bbInitIli9488};
    const char* names[] = {"ILI9341", "ILI9342", "ST7789", "ST7796", "ILI9488"};
    uint8_t i = 0;
    while (true) {
        Serial.print("Trying controller ");
        Serial.println(names[i]);
        Serial.print("Backlight blink code: ");
        Serial.println(i + 1);
        for (uint8_t b = 0; b <= i; b++) {
            digitalWrite(TFT_BL, LOW);
            delay(180);
            digitalWrite(TFT_BL, HIGH);
            delay(220);
        }
        delay(800);
        inits[i]();
        bbFillBars();
        Serial.println("Showing this controller for 12 seconds");
        delay(12000);
        i = (i + 1) % 5;
    }
}
#else

static uint8_t spiReadTftByte(uint8_t command, uint8_t dummyReads) {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, LOW);
    SPI.transfer(command);
    digitalWrite(TFT_DC, HIGH);
    uint8_t value = 0;
    for (uint8_t i = 0; i < dummyReads; i++) {
        value = SPI.transfer(0x00);
    }
    digitalWrite(TFT_CS, HIGH);
    SPI.endTransaction();
    return value;
}

static void probeTftId() {
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, -1);
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_RST, OUTPUT);
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TOUCH_CS, HIGH);

    Serial.println("Probing TFT over SPI...");
    Serial.print("RDDID bytes: ");
    for (uint8_t i = 1; i <= 4; i++) {
        uint8_t b = spiReadTftByte(0x04, i);
        Serial.printf("%02X ", b);
    }
    Serial.println();

    Serial.print("RDDST bytes: ");
    for (uint8_t i = 1; i <= 4; i++) {
        uint8_t b = spiReadTftByte(0x09, i);
        Serial.printf("%02X ", b);
    }
    Serial.println();

    Serial.printf("RDPIXFMT: %02X\n", spiReadTftByte(0x0C, 2));
    Serial.printf("RDDIM:    %02X\n", spiReadTftByte(0x0D, 2));
}

static void wigglePin(uint8_t pin, const char* name) {
    Serial.print("Wiggle ");
    Serial.print(name);
    Serial.print(" GPIO");
    Serial.println(pin);
    pinMode(pin, OUTPUT);
    for (uint8_t i = 0; i < 6; i++) {
        digitalWrite(pin, HIGH);
        delay(350);
        digitalWrite(pin, LOW);
        delay(350);
    }
}

static void wiggleTftPins() {
    Serial.println("=== TFT PIN WIGGLE TEST ===");
    Serial.println("Each signal toggles for about 4 seconds.");
    wigglePin(TFT_CS, "TFT_CS");
    wigglePin(TFT_DC, "TFT_DC");
    wigglePin(TFT_RST, "TFT_RST");
    wigglePin(TFT_MOSI, "TFT_MOSI");
    wigglePin(TFT_SCLK, "TFT_SCLK");
    wigglePin(TFT_BL, "TFT_BL");
    Serial.println("=== PIN WIGGLE DONE ===");
}

static void diagBacklight(uint8_t level, const char* label) {
    Serial.print("BL ");
    Serial.println(label);
    digitalWrite(TFT_BL, level);
    delay(2000);
}

static void runTftDiagnostic() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== TFT DIAGNOSTIC ONLY ===");
    Serial.println("Pins: CS=10 DC=14 RST=21 BL=47 SCK=12 MOSI=11 MISO=13 TOUCH_CS=8");

    pinMode(TFT_BL, OUTPUT);
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_RST, OUTPUT);
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TOUCH_CS, HIGH);

    diagBacklight(HIGH, "HIGH for 2s");
    diagBacklight(LOW, "LOW for 2s");
    diagBacklight(HIGH, "HIGH while testing TFT");

    wiggleTftPins();

    Serial.println("Manual reset pulse");
    digitalWrite(TFT_RST, HIGH);
    delay(20);
    digitalWrite(TFT_RST, LOW);
    delay(50);
    digitalWrite(TFT_RST, HIGH);
    delay(150);

    probeTftId();

    static TFT_eSPI tft;
    Serial.println("tft.init()");
    tft.init();
    tft.setRotation(0);
    tft.invertDisplay(false);
    Serial.println("Color loop starts now");

    const uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK, TFT_YELLOW};
    const char* names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK", "YELLOW"};
    uint8_t i = 0;
    while (true) {
        Serial.print("fill ");
        Serial.println(names[i]);
        tft.fillScreen(colors[i]);
        i = (i + 1) % 6;
        delay(1000);
    }
}
#endif
#endif

// ── Task-Zeitstempel ──────────────────────────────────────
static unsigned long tScheduler  = 0;
static unsigned long tEcowitt    = 0;
static unsigned long tConfigSync = 0;
static unsigned long tSensorUp   = 0;
static unsigned long tStatus     = 0;
static unsigned long tDisplay    = 0;
static unsigned long tTouch      = 0;
static unsigned long tWdt        = 0;

void setup() {
#ifdef TOUCH_DIAG_ONLY
    runTouchDiagnostic();
#endif

#ifdef TFT_DIAG_ONLY
#if TFT_DIAG_BITBANG_ONLY
    runBitbangDiagnostic();
#else
    runTftDiagnostic();
#endif
#endif

    Serial.begin(115200);
    delay(100);
    Serial.println("=== SETUP START ===");

    // 1. Failsafe: alle Ventile schließen BEVOR alles andere
    valve::init();

    // 2. Letzte Config aus NVS laden, damit das Display sofort sinnvolle Daten hat
    cfg::loadFromNVS();

    // 3. Display früh initialisieren: zeigt auch dann Diagnose, wenn WiFi hängt
    display::init();

    // 4. RTC früh lesen, damit Zeitpläne auch ohne Netzwerk eine Uhr haben
    rtc::init();
    rtc::syncSystemFromRtc();

    // 5. WiFi + NTP  (kein WDT aktiv – setup darf beliebig lang blockieren)
    wifi::connect();
    ota::init();

    // 6. Config und Eventlog vom Backend holen
    if (wifi::isConnected()) {
        cfg::sync();
        events::loadRecentFromBackend();
    }

    // 7. Erster Ecowitt-Poll
    ecowitt::poll();
    display::pushSensorHistory();

    // 8. Display nach Netzwerk-/Sensordaten aktualisieren
    display::refresh();

    // 9. Watchdog erst NACH setup() aktivieren – schützt ab jetzt den loop()
    wdt::init();

    Serial.println("Bewässerung bereit.");
}

void loop() {
    unsigned long now = millis();
    static String lastValveState = valve::stateStr();
    static bool eventHistoryLoaded = (events::recentCount() > 0);

    // Watchdog füttern (alle 4s)
    if (now - tWdt >= INTERVAL_WDT_MS) {
        wdt::feed();
        tWdt = now;
    }

    // Touch-Polling (alle 50ms)
    if (now - tTouch >= INTERVAL_TOUCH_MS) {
        display::handleTouch();
        tTouch = now;
    }

    // WiFi-Reconnect prüfen
    wifi::loop();
    ota::handle();

    // Während des eigentlichen OTA-Uploads keine HTTP-Syncs oder Scheduler-
    // Arbeit dazwischenwerfen. Nur die OTA-Seite darf den Fortschritt nachziehen.
    if (ota::running()) {
        if (now - tDisplay >= 500UL) {
            display::periodicRefresh();
            tDisplay = now;
        }
        return;
    }

    // Ventil-Timeout überwachen
    valve::update();

    // Scheduler-Jobs abarbeiten
    scheduler::update();

    // Scheduler-Tick (jede Sekunde, damit Minutenstarts nicht verpasst werden)
    if (now - tScheduler >= INTERVAL_SCHEDULER_MS) {
        scheduler::tick();
        tScheduler = now;
    }

    String currentValveState = valve::stateStr();
    if (currentValveState != lastValveState) {
        display::refresh();
        events::postStatus();
        events::flush();
        lastValveState = currentValveState;
    }

    // Ecowitt-Poll (alle 5min)
    if (now - tEcowitt >= INTERVAL_ECOWITT_MS) {
        ecowitt::poll();
        display::pushSensorHistory();
        events::uploadSensors();
        tEcowitt = now;
    }

    // Config-Sync (jede Minute)
    if (now - tConfigSync >= INTERVAL_CONFIG_MS) {
        if (cfg::sync()) display::refresh();
        tConfigSync = now;
    }

    // Status-Heartbeat (jede Minute)
    if (now - tStatus >= INTERVAL_STATUS_MS) {
        if (!eventHistoryLoaded && wifi::isConnected()) {
            eventHistoryLoaded = events::loadRecentFromBackend();
            if (eventHistoryLoaded) display::refresh();
        }
        events::postStatus();
        events::flush();
        tStatus = now;
    }

    // Display-Refresh (ruhig, ohne statische Seiten ständig neu zu zeichnen)
    if (now - tDisplay >= INTERVAL_DISPLAY_MS) {
        display::periodicRefresh();
        tDisplay = now;
    }
}
