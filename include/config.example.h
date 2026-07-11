#pragma once

// ========================================================
// Multicontrol Duo ESP32-S3 - Beispiel-Konfiguration
// Kopieren nach include/config.h und lokale Werte eintragen.
// include/config.h ist absichtlich in .gitignore, damit keine Secrets
// ins Repository gelangen.
// ========================================================

// -- Credentials -----------------------------------------
#define WIFI_SSID        "DEIN_WLAN"
#define WIFI_PASS        "DEIN_WLAN_PASSWORT"

#define API_BASE_URL     "https://deine-domain.de/api/irrigation/device"   // kein trailing slash
#define ESP_API_KEY      "<IRRIGATION_DEVICE_TOKEN>"
#define API_AUTH_BEARER  1   // Jninty Device API: Authorization: Bearer <token>

#define MQTT_ENABLED      0
#define MQTT_HOST         "tofu.creano.de"
#define MQTT_PORT         1883
#define MQTT_DEVICE_ID    "esp32-01"
#define MQTT_TOPIC_PREFIX "irrigation"
#define MQTT_USER         "irrigation_esp"
#define MQTT_PASS         "<MQTT_IRRIGATION_DEVICE_PASS>"

#define ECOWITT_IP       "10.0.0.50"   // GW1200 lokale IP, nur IP ohne http://
#define ECOWITT_PORT     8090

// -- GPIO: Relaisboard ------------------------------------
// Die acht bisherigen H-Bruecken-Ausgaenge koennen direkt auf IN1-IN8
// des Relaisboards gelegt werden. Genutzt werden initial Zone 1-6.
#define V1_IN1  4   // IN1 / Zone 1
#define V1_IN2  5   // IN2 / Zone 2
#define V2_IN1  6   // IN3 / Zone 3
#define V2_IN2  7   // IN4 / Zone 4
#define V3_IN1  15  // IN5 / Zone 5
#define V3_IN2  16  // IN6 / Zone 6
#define V4_IN1  17  // IN7 / Reserve
#define V4_IN2  18  // IN8 / Reserve

#define RELAY_ACTIVE_LOW 1
#define RELAY_ZONE_COUNT 6

// -- GPIO: TFT + Touch (via platformio.ini build_flags) --
// ILI9341: CS=41, DC=42, RST=21, BL=47
// XPT2046: T_CS=8, T_IRQ=9
// SPI shared: MOSI=38, SCK=40, MISO=39

// -- GPIO: DS3231 RTC (I2C) ------------------------------
#define RTC_SDA  1
#define RTC_SCL  2
#define RTC_I2C_ADDR 0x68

#define VALVE_OPEN_PULSE_MS   250      // Altwert, im Relaisbetrieb nicht genutzt
#define VALVE_CLOSE_PULSE_MS  63       // Altwert, im Relaisbetrieb nicht genutzt
#define VALVE_CLOSE_PWM_FREQ  20000    // 20 kHz
#define VALVE_CLOSE_PWM_BITS  8        // 8-bit, 0-255
#define VALVE_CLOSE_DUTY      51       // Altwert, im Relaisbetrieb nicht genutzt
#define VALVE_SEQ_PAUSE_MS    2000     // Altwert, im Relaisbetrieb nicht genutzt

// -- Task-Intervalle (ms) ---------------------------------
#define INTERVAL_SCHEDULER_MS    1000UL
#define INTERVAL_ECOWITT_MS    300000UL
#define INTERVAL_CONFIG_MS      30000UL
#define INTERVAL_SENSOR_UP_MS  300000UL
#define INTERVAL_STATUS_MS      60000UL
#define INTERVAL_DISPLAY_MS      1000UL
#define INTERVAL_TOUCH_MS          50UL
#define INTERVAL_WDT_MS          4000UL

// -- Watchdog ---------------------------------------------
#define WDT_TIMEOUT_MS  8000

// -- Stabilitaets-Monitoring ------------------------------
// Speichert ca. alle 5 Minuten Uptime/Heap in NVS. Nach einem Watchdog-
// Reset sieht man dadurch, wie lange der ESP vorher ungefaehr lief.
#define STABILITY_SNAPSHOT_MS 300000UL
#define CRASH_OPEN_LOCKOUT_MS 120000UL
#define CRASH_CLOSE_EXTRA_PASSES 2

// -- Sensor-Freshness -------------------------------------
#define SENSOR_STALE_MS    900000UL
#define ECOWITT_DEAD_MS   1800000UL
#define BACKEND_DEAD_MS    300000UL
#define BACKEND_ERROR_BACKOFF_MS 120000UL

// -- Scheduler bei fehlenden Bodensensoren ----------------
// SCHEDULER_MISSING_SENSOR_MODE:
//   0 = Zeitplan skippen, wenn keine frischen Bodensensorwerte vorliegen
//   1 = Zeitplan trotzdem starten, mit SCHEDULER_SENSOR_FALLBACK_PERCENT
// SCHEDULER_IGNORE_SENSOR_CHECKS:
//   0 = normale Sensorchecks aktiv
//   1 = Sensorchecks komplett ignorieren (bewusster Zeitplanbetrieb)
#define SCHEDULER_MISSING_SENSOR_MODE       1
#define SCHEDULER_SENSOR_FALLBACK_PERCENT 100
#define SCHEDULER_IGNORE_SENSOR_CHECKS      0

// -- Display ----------------------------------------------
#define DISPLAY_BACKLIGHT_DIM_MS 600000UL  // nach 10min ausschalten
#define DISPLAY_BL_FULL            255
#define DISPLAY_BL_DIM               0     // aus

// -- HTTP --------------------------------------------------
#define HTTP_TIMEOUT_MS  3000
#define DEBUG_API_SYNC   0

// -- WhatsApp Benachrichtigungen --------------------------
// CallMeBot WhatsApp API: https://www.callmebot.com/blog/free-api-whatsapp-messages/
// Die API-Keys gehoeren jeweils zur aktivierten Telefonnummer.
#define WHATSAPP_NOTIFICATIONS_ENABLED 0
#define WHATSAPP_SEND_INTERVAL_MS      5000UL
#define WHATSAPP_QUEUE_SIZE            8

#define WHATSAPP_RECIPIENT_1_ENABLED 0
#define WHATSAPP_RECIPIENT_1_PHONE   "+491701234567"
#define WHATSAPP_RECIPIENT_1_API_KEY "callmebot-api-key-1"

#define WHATSAPP_RECIPIENT_2_ENABLED 0
#define WHATSAPP_RECIPIENT_2_PHONE   "+491701234568"
#define WHATSAPP_RECIPIENT_2_API_KEY "callmebot-api-key-2"

// -- OTA ---------------------------------------------------
#define OTA_HOSTNAME           "multicontrol-duo-esp32"
#define OTA_PASSWORD           "ota-passwort-aendern"
#define OTA_ENABLE_WINDOW_MS   300000UL

// -- Event-Puffer -----------------------------------------
#define EVENT_BUF_SIZE  100

// -- NVS ---------------------------------------------------
#define NVS_NAMESPACE  "bewaess"

// -- NTP ---------------------------------------------------
#define NTP_SERVER    "pool.ntp.org"
#define TZ_STRING     "CET-1CEST,M3.5.0,M10.5.0/3"
