# Multicontrol Duo ESP32-S3

ESP32-S3 Firmware fuer die Multicontrol-Duo-Bewaesserungssteuerung.

Aktuelle Firmware-Version: `2.2.9`

Die Firmware steuert sechs Bewaesserungszonen ueber ein Relaisboard, zeigt den Zustand auf einem 3.2" SPI-TFT an, synchronisiert Sensorwerte/Events/Kommandos mit dem Jninty-Bewaesserungsmodul und unterstuetzt OTA-Updates.

## Setup

Die echte Konfiguration liegt lokal in `include/config.h` und wird nicht versioniert, weil dort WLAN- und API-Secrets stehen.

Beim ersten Checkout:

```bash
cp include/config.example.h include/config.h
```

Danach in `include/config.h` mindestens setzen:

- `WIFI_SSID`
- `WIFI_PASS`
- `API_BASE_URL`
- `ESP_API_KEY`
- `ECOWITT_IP`
- `OTA_PASSWORD`

## Aktueller Funktionsumfang

- TFT UI mit Dashboard, Sensorhistorie, manueller Ventilsteuerung, Systemseite, Diagnose, Eventlog, OTA, Ausgangsstatus und Touch-Kalibrierung.
- Web Backend/Frontend fuer Dashboard, Zonen, Zeitprogramme, manuelle Befehle, Eventlog und Historie.
- 6 Zonen ueber Relaisboard auf den bisherigen H-Bruecken-GPIOs GPIO4/5/6/7/15/16.
- GW1200/Ecowitt Sensorwerte fuer Bodenfeuchte, Bodentemperatur und EC.
- DS3231 RTC als robuste lokale Uhr.
- ArduinoOTA fuer Firmware-Updates per WLAN.
- Event-Sync zwischen ESP und Backend: ESP schreibt Events ins Backend und laedt beim Start die letzten Events.
- Web-Konfiguration kann auf der Zonen-Seite als JSON exportiert werden.
- Basis fuer WhatsApp-Benachrichtigungen ueber CallMeBot mit zwei konfigurierbaren Empfaengern.

## Jninty Backend

Die Firmware kann mit dem nativen Jninty-Bewaesserungsmodul sprechen.

In `include/config.h` muessen dazu gesetzt sein:

```cpp
#define API_BASE_URL     "https://deine-domain.de/api/irrigation/device"
#define ESP_API_KEY      "<IRRIGATION_DEVICE_TOKEN aus docker-compose>"
#define API_AUTH_BEARER  1
```

Der Token muss identisch mit `IRRIGATION_DEVICE_TOKEN` im Jninty `server.environment` sein.

Genutzte ESP-Endpunkte:

- `GET /config`
- `GET /commands`
- `GET /events?limit=12`
- `POST /status`
- `POST /sensors`
- `POST /events`
- `POST /commands/:id/done`

## Dashboard Plan-/Skip-Anzeige

Das Dashboard zeigt pro Zone den naechsten Lauf und die aktuelle Prognose.

Moegliche Meldungen:

- `Plan: wird laufen`: Zone ist aktiv, Sensor ist frisch, Boden ist nicht zu kalt und Feuchte liegt unter dem Schwellwert.
- `Skip: Zone inaktiv`: Zone ist deaktiviert.
- `Skip: keine Sensor Daten`: Fuer den zugeordneten WH52-Kanal liegen keine frischen Daten vor.
- `Skip: Boden zu kalt <Wert>C`: Bodentemperatur liegt unter der konfigurierten Mindesttemperatur.
- `Skip: Boden zu feucht <Wert>%`: Bodenfeuchte liegt am oder ueber dem konfigurierten Schwellwert.
- `Skip: es regnet <Wert>mm`: Regen-Sperre ist aktiv, wenn im Scheduler/ESP entsprechend bewertet.

## Betrieb ohne Bodensensoren

Wenn anfangs noch keine WH52/Bodensensoren vorhanden sind, kann der Scheduler trotzdem im Zeitplanbetrieb laufen.

In `include/config.h`:

```cpp
#define SCHEDULER_MISSING_SENSOR_MODE       1
#define SCHEDULER_SENSOR_FALLBACK_PERCENT 100
#define SCHEDULER_IGNORE_SENSOR_CHECKS      0
```

Bedeutung:

- `SCHEDULER_MISSING_SENSOR_MODE 0`: Zeitplan wird geskippt, wenn keine frischen Bodensensorwerte vorliegen.
- `SCHEDULER_MISSING_SENSOR_MODE 1`: Zeitplan läuft trotzdem mit dem angegebenen Fallback-Prozent.
- `SCHEDULER_SENSOR_FALLBACK_PERCENT 100`: Bei fehlenden Sensoren volle geplante Dauer.
- `SCHEDULER_IGNORE_SENSOR_CHECKS 1`: Sensorchecks komplett ignorieren. Nur bewusst fuer reinen Zeitplanbetrieb verwenden.

Sobald frische Sensordaten vorhanden sind, greifen die normalen Prüfungen für Bodentemperatur, Bodenfeuchte und Regen.

Im Web-Dashboard ist die Sync-/Statuskarte am Desktop oben und mobil unten. Die Reihenfolge ist:

`Ventile`, `ESP`, `GW1200`, `Sync`

Die Ventile werden als `V1 On | V2 Off | V3 Off | V4 On` angezeigt. `On` ist gruen.

## Eventlog

Events werden einzeilig angezeigt und zwischen ESP und Backend synchronisiert.

Filter im Web-Frontend:

- `Alle`
- `Oeffnen`
- `Schliessen`
- `Skip`
- `Manuell`
- `Scheduler`
- Zone-Auswahl

Der Scheduler-Filter umfasst geplante Laeufe und sensorbedingte Scheduler-Skips.

Boot-/Crash-Diagnosen werden als `reset`-Events geloggt. Im Detailfeld stehen
Resetgrund, Boot-/Crash-Zaehler, letzte Uptime, Breadcrumb-Stage und Heap, z.B.
`R task_wdt B279 C275 U186902 S http:config:send H257584`.

## WhatsApp Benachrichtigungen

Die Firmware enthaelt eine deaktivierte Basis fuer WhatsApp-Benachrichtigungen ueber die Bibliothek `Callmebot ESP32`.
Konkrete Systemereignisse werden ueber `notify::enqueue(...)` in eine Queue gestellt und nacheinander an alle aktiven Empfaenger versendet.

In `include/config.h`:

```cpp
#define WHATSAPP_NOTIFICATIONS_ENABLED 0
#define WHATSAPP_SEND_INTERVAL_MS      5000UL
#define WHATSAPP_QUEUE_SIZE            8

#define WHATSAPP_RECIPIENT_1_ENABLED 0
#define WHATSAPP_RECIPIENT_1_PHONE   "+491701234567"
#define WHATSAPP_RECIPIENT_1_API_KEY "callmebot-api-key-1"

#define WHATSAPP_RECIPIENT_2_ENABLED 0
#define WHATSAPP_RECIPIENT_2_PHONE   "+491701234568"
#define WHATSAPP_RECIPIENT_2_API_KEY "callmebot-api-key-2"
```

Bedeutung:

- `WHATSAPP_NOTIFICATIONS_ENABLED 1`: WhatsApp-Queue aktivieren.
- Pro Empfaenger muss `*_ENABLED 1`, Telefonnummer und passender CallMeBot-API-Key gesetzt sein.
- Bei einer spaeteren Ereignis-Anbindung wird eine Nachricht fuer alle aktiven Empfaenger in die Queue gestellt.
- `WHATSAPP_SEND_INTERVAL_MS` begrenzt den Abstand zwischen zwei API-Aufrufen, damit der Loop nicht durch mehrere Nachrichten direkt hintereinander blockiert.

Aktuell angebundene Ereignisse:

- ESP Neustart / Online mit IP, Reset-Ursache und Firmware-Version.
- Watchdog/Crash/Brownout erkannt mit Reset-Ursache.
- Backend laenger als `BACKEND_DEAD_MS` nicht erreichbar und wieder erreichbar.
- GW1200 laenger als `ECOWITT_DEAD_MS` nicht erreichbar und wieder erreichbar.
- RTC/DS3231 beim Start nicht erreichbar oder Uhrzeit ungueltig.

## Konfigurations-Export

Auf der Web-Seite `Zonen` gibt es unten den Button `Konfiguration exportieren`.

Der Export erzeugt eine JSON-Datei mit:

- Export-Zeitstempel
- Zonen
- Zeitprogrammen

## OTA Update

OTA funktioniert erst, nachdem die Firmware mindestens einmal per USB mit der OTA-Partitionstabelle geflasht wurde. Das ist ab dieser Version erledigt.

### OTA am Display starten

1. Am TFT `System -> OTA Update` oeffnen.
2. `OTA STARTEN` tippen.
3. Das OTA-Fenster bleibt 5 Minuten offen.
4. Den Upload im Terminal starten.

### OTA Upload per Terminal

Im Firmware-Ordner ausfuehren:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run -e esp32s3_ota -t upload
```

PlatformIO baut die Firmware automatisch und laedt sie anschliessend per WLAN hoch. Ein separates Image muss vorher nicht erzeugt werden.

Falls `bewaesserung-esp32.local` nicht aufgeloest wird, die IP-Adresse von der OTA-Seite am TFT verwenden:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run -e esp32s3_ota -t upload --upload-port 10.18.3.xxx
```

### USB Upload

Falls OTA nicht erreichbar ist, bleibt USB als Fallback:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run -e esp32s3 -t upload
```

Nach Aenderungen an `partitions.csv` immer per USB flashen.

## Relais-Test per USB

Fuer einen reinen Hardwaretest ohne Scheduler/WLAN/Touch gibt es ein separates
PlatformIO-Environment:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run -e esp32s3_relay_diag -t upload
```

Die Diagnose-Firmware schaltet IN1 bis IN6 nacheinander fuer kurze Zeit ein.
Danach wieder die normale Firmware flashen:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run -e esp32s3 -t upload
```
