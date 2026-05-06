# Multicontrol Duo ESP32-S3

ESP32-S3 Firmware fuer die Multicontrol-Duo-Bewaesserungssteuerung.

Die Firmware steuert vier bistabile Ventile ueber DRV8871-Treiber, zeigt den Zustand auf einem 3.2" SPI-TFT an, synchronisiert Sensorwerte/Events/Kommandos mit dem Jninty-Bewaesserungsmodul und unterstuetzt OTA-Updates.

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

- TFT UI mit Dashboard, Sensorhistorie, manueller Ventilsteuerung, Systemseite, Diagnose, Eventlog, OTA, Ventilsetup und Touch-Kalibrierung.
- Web Backend/Frontend fuer Dashboard, Zonen, Zeitprogramme, manuelle Befehle, Eventlog und Historie.
- 4 Ventile ueber DRV8871 Treiber.
- GW1200/Ecowitt Sensorwerte fuer Bodenfeuchte, Bodentemperatur und EC.
- DS3231 RTC als robuste lokale Uhr.
- ArduinoOTA fuer Firmware-Updates per WLAN.
- Event-Sync zwischen ESP und Backend: ESP schreibt Events ins Backend und laedt beim Start die letzten Events.
- Web-Konfiguration kann auf der Zonen-Seite als JSON exportiert werden.

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
