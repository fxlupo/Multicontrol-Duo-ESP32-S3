# Hunter PVG / Relay / MQTT Umbauplan

Stand: 2026-07-12

## Produktivstand 2026-07-12

- Das Board ist produktiv online:
  - IP: `192.168.10.116`
  - OTA ist moeglich und wurde mehrfach erfolgreich genutzt.
  - Firmware `2.2.26` wurde am 2026-07-12 per OTA geflasht.
- Web-App/Backend `jninty-de` Version `1.9.20` ist produktiv deployed.
- MQTT ist der normale Live-Kommunikationsweg:
  - Broker: `tofu.creano.de:1883`
  - Prefix: `irrigation/esp32-01/#`
  - Status: `irrigation/esp32-01/status`, retained.
  - Config: `irrigation/esp32-01/config`, retained.
  - Events: `irrigation/esp32-01/events`.
  - Sensoren: `irrigation/esp32-01/sensors`.
  - Commands: `irrigation/esp32-01/commands/<id>` plus
    `commands/<id>/result`.
- HTTP ist im Normalbetrieb weitgehend raus:
  - `/config` bleibt nur Fallback, solange keine retained MQTT-Config angekommen
    ist.
  - Status/Sensoren/Events werden nur noch per HTTP gesendet, wenn MQTT-Publish
    nicht klappt.
  - `/commands` bleibt bewusst als Safety-Net aktiv.
- Das Relaisboard ist an den acht Ausgaengen angeschlossen, die vorher die
  H-Bruecken gesteuert haben.
- Lokales Pinlayout:
  - GPIO4 -> IN1 / Zone 1
  - GPIO5 -> IN2 / Zone 2
  - GPIO6 -> IN3 / Zone 3
  - GPIO7 -> IN4 / Zone 4
  - GPIO15 -> IN5 / Zone 5
  - GPIO16 -> IN6 / Zone 6
  - GPIO17/18 -> IN7-IN8 Reserve
- `src/valve_driver.cpp` wurde auf Relais-Dauerbetrieb umgestellt und danach
  auf 6 Zonen erweitert:
  - Boot-Zustand: alle Relais aus.
  - `open(zone)`: genau ein Relais an.
  - `close(zone)`/`closeAll()`: Relais aus, ohne Pulse/PWM.
  - Default: `RELAY_ACTIVE_LOW 1`, `RELAY_ZONE_COUNT 6`.
- Relais-Diagnose:
  - `esp32s3_relay_diag` schaltet IN1-IN6 nacheinander per USB-Testfirmware.
- Relais-Hardwaretest erfolgreich:
  - Ausgangs-LEDs schalten.
  - Relais ziehen nach korrekter `VCC` + `JD-VCC` 5V-Versorgung an.
  - COM/NO hat bei aktivem Relais Durchgang.
  - Normale 6-Zonen-Firmware wurde nach dem Test wieder per USB geflasht.
- 24VAC-/Ventiltest erfolgreich:
  - Hunter-24VAC-Ventile schalten ueber Relais.
  - Manuelle Zone-1-6-Steuerung funktioniert.
  - `ALLE STOP`/alle Relais aus bleibt der sichere Zielzustand.
- GW1200 liefert aktuell Werte fuer 5 Bodenfeuchtesensoren. Fuer den ersten
  Produktivschritt sollen WH52/GW1200 Channel 1-4 fest an Zone/Ventil 1-4
  gekoppelt werden. Channel 5 steckt in der freien Flaeche und wird als
  gemeinsamer Regen-/Feuchteindikator fuer Zone 5/6 genutzt.
- Sensorchecks sind in der Firmware wieder aktiv:
  - `SCHEDULER_IGNORE_SENSOR_CHECKS = 0`
  - `SCHEDULER_MISSING_SENSOR_MODE = 1` bleibt als Fallback, falls ein
    Sensorwert alt/fehlend ist.
- Echtes GW1200-JSON wurde geprueft: Bodenwerte kommen unter `ch_ec`
  (`channel`, `humidity`, `temp`, `ec`). Der Parser wurde darauf erweitert.
- Produktivbeobachtung 2026-07-11:
  - Manuelle Web-App-Steuerung reagiert mit minimaler Verzoegerung.
  - Scheduler-Laeufe werden sauber an den ESP uebertragen und ausgefuehrt.
  - MQTT-Umstellung ist im praktischen Betrieb die bessere Entscheidung.

## Wartungsfixes 2.2.21 bis 2.2.25

Die Versionen `2.2.21` bis `2.2.25` sind gezielte Haertungen auf dem
produktiven MQTT-/Relais-Stand:

- `2.2.21`: `wdt::feed()` im HTTP-Command-Ack-Loop. Mehrere offene Commands
  koennen jeweils bis zu `HTTP_TIMEOUT_MS` blockieren; der Watchdog wird nun
  vor jedem Ack-HTTP-Call gefuettert.
- `2.2.22`: GPIO-Initialisierung im Relais-Treiber korrigiert. Erst
  `pinMode(OUTPUT)`, danach `digitalWrite(inactiveLevel)`, damit Relais beim
  Boot nicht kurz undefiniert sind.
- `2.2.23`: `valve::open()` lehnt `maxDurationMs == 0` ab. Dadurch kann ein
  Command oder Bug kein Ventil ohne Auto-Close-Timer offen lassen.
- `2.2.24`: `run_once` interpretiert `duration_min` wieder als Minuten statt
  Sekunden. Dauer wird auf 60 bis 7200 Sekunden begrenzt.
- `2.2.25`: OTA-Passwort steht nicht mehr in `platformio.ini`, sondern wird
  beim OTA-Upload ueber `OTA_PASSWORD` aus der Umgebung gelesen. Das Passwort
  muss zum lokalen, nicht versionierten `include/config.h` passen.
- `2.2.26`: M3/H4/M1/M2/M4 umgesetzt:
  - Event-Puffer wird bei MQTT-Verbindung nur geleert, wenn der MQTT-Publish
    tatsaechlich erfolgreich war.
  - OTA-App wird ohne Stresstest erst nach stabiler Laufzeit, WiFi und ohne
    offenes Ventil per `esp_ota_mark_app_valid_cancel_rollback()` gueltig
    markiert.
  - Schedule-Trigger-Speicher verdrängt bei vollen Slots per Ring statt neue
    Trigger still zu verwerfen.
  - NVS-Konfig speichert Schema- und Struct-Groessen und ignoriert alte Blobs
    nach inkompatiblen Firmware-Aenderungen.
  - Commands werden nach lokaler Ausfuehrung aus der aktiven Queue genommen;
    `done` wird separat retrybar nachgemeldet.
- Jninty `1.9.20` ist aktueller Git-/Produktivstand:
  - Tasks-Filter laufen in SQL statt im JavaScript-Speicher.
  - MQTT-Command-Result-RegExp wird einmal beim Handler-Setup kompiliert.

Lokale Pruefung am 2026-07-12:

- Build `esp32s3` erfolgreich.
- Build `esp32s3_relay_diag` erfolgreich.
- OTA per mDNS `bewaesserung-esp32.local` schlug lokal fehl, direkter Upload
  an `192.168.10.116` war erfolgreich.
- Nach OTA antwortet `192.168.10.116` per Ping.
- `firmwareVersion` im MQTT-/HTTP-Status und die Boot-Benachrichtigung melden
  `2.2.26`.
- OTA-Upload `2.2.26` an `192.168.10.116` lieferte `Result: OK`; direkter
  Ping danach: 3/3 Pakete, 0% Verlust.
- Nach 130 Sekunden Stabilitaetsfrist erneut gepingt: 3/3 Pakete, 0%
  Verlust.
- Die Relais-Diagnose nutzt nach dem `maxDurationMs == 0`-Failsafe wieder eine
  begrenzte Testdauer statt `0`, damit `esp32s3_relay_diag` weiter schaltet.
- Produktivpfade fuer manuelle `open`-Commands und Scheduler-Jobs uebergeben
  weiterhin eine endliche Dauer.

## Abschlussstand

Erledigt:

1. Hardware:
   - Relaisboard produktiv an den acht ehemaligen H-Bruecken-Ausgaengen.
   - Zone 1-6 schalten ueber Relais 1-6.
   - Relais 7-8 bleiben Reserve.
   - 24VAC-/Ventiltest erfolgreich.
2. Sensorik:
   - V1-V4 nutzen WH52/GW1200 Channel 1-4.
   - V5/V6 nutzen gemeinsam WH52/GW1200 Channel 5.
   - Kein Channel 6 in der History.
3. Firmware:
   - Relais-Dauerbetrieb statt H-Bruecken-Pulse.
   - Runtime-State mit `idle`/`running`/`queued`, Restzeit und Queue-Laenge.
   - Lokale Sensor-History vom TFT entfernt; TFT zeigt Live-Werte.
   - Manual-Close-Dauer korrigiert.
   - MQTT-Transport fuer Config, Status, Sensoren, Events und Commands.
4. Web-App/Backend:
   - 6 Zonen.
   - 7-Tage-History mit echten Sensordaten.
   - Program Preview.
   - Controller enabled/disabled und Rain Delay.
   - Run-Once/Testlaeufe.
   - MQTT-Subscriber/Publisher produktiv aktiv.
   - Docker Compose liest Bewaesserungs-/MQTT-Werte aus `.env`.
5. Tests:
   - Retained Config enthaelt echte `zones`, `schedules`, `control`.
   - MQTT-Status meldet Firmware `2.2.26`, `valveStates: 000000`,
     Queue leer.
   - MQTT-Command `close_all` lieferte `acked` und `done`.
   - Manuelle Web-App- und Scheduler-Tests erfolgreich.

Bewusst offen:

1. HTTP `/commands` bleibt als Safety-Net aktiv.
2. TLS fuer MQTT wird spaeter entschieden; aktuell TCP mit Auth/ACL.
3. Soak-Test ueber die naechsten Nachtlaeufe passiert im normalen
   Produktivbetrieb.

## Folgeplan ab 2026-07-11

Der Relais-/6-Zonen-/Ecowitt-Produktivstand ist erreicht:

- 6 Hunter-PVG-Zonen schalten ueber Relais.
- V1-V4 nutzen WH52/GW1200 Channel 1-4.
- V5/V6 nutzen gemeinsam WH52/GW1200 Channel 5.
- Web-App `jninty-de` Version `1.9.6` ist online.
- 7-Tage-History in der Web-App funktioniert mit echten Daten.
- ESP/TFT zeigt keine lokale Sensor-History mehr, sondern nur Live-Werte.
- Manuelle Event-Dauer ist per OTA gefixt und getestet.
- ESP meldet Runtime-State/Queue mit `idle`/`running`/`queued`, Restzeit und
  Queue-Laenge; Web-App Version `1.9.7` zeigt diese Werte an.
- Run-Once/Testprogramm ist umgesetzt: Web-App Version `1.9.8` kann
  Testlaeufe fuer alle aktiven Zonen mit 10/20/30 Sekunden starten; Firmware
  `2.2.11` legt diese Laeufe in die Runtime-Queue.
- Program Preview sowie Scheduler-Sperre/Rain Delay sind umgesetzt:
  Web-App Version `1.9.9` zeigt die 7-Tage-Vorschau und steuert
  `controllerEnabled`/`rainDelayUntil`; Firmware `2.2.13` beruecksichtigt die
  Flags bei geplanten Laeufen.

Naechste sinnvolle Schritte, falls wir weiter ausbauen:

1. MQTT bleibt unten:
   - erst wenn HTTP/Polling im Produktivbetrieb wirklich stoert.
   - dann zunaechst Status/Events/Sensoren, spaeter Commands.

Nicht mehr verfolgen:

- Web-App Eventlog/History-Komfortausbau mit weiteren Filtern/Summen/Export.
- Echte Langzeit-History auf dem ESP/TFT.
- Nachtlauf-Beobachtung als eigener Todo-Punkt; das passiert im normalen
  Produktivbetrieb.

## Zielbild

Die bistabilen Ventile und DRV8871-H-Bruecken werden entfernt. Die Anlage wird
auf normale 24VAC-Bewaesserungsventile vom Typ Hunter PVG-101 mit 24VAC-
Magnetspulen umgebaut.

Der ESP32-S3 bleibt als lokale Steuerung mit TFT, Touch, RTC, Ecowitt/GW1200,
Scheduler, Eventlog und OTA erhalten. Die Ventilansteuerung wird von
H-Bruecken-Pulsen auf Relais-Dauerbetrieb umgestellt:

- Ventil offen = Relais angezogen = 24VAC liegt an der Magnetspule.
- Ventil geschlossen = Relais abgefallen = 24VAC ist getrennt.
- Es bleibt bei der Logik: maximal ein Ventil gleichzeitig offen.
- Das 8-Relais-Board wird zuerst fuer 6 Zonen genutzt; Relais 7-8 bleiben
  Reserve.
- ESP-Backend-Kommunikation wird von HTTP-Polling auf MQTT umgebaut.

## Hardware-Umbau

### Versorgung

- 24VAC Trafo versorgt die Ventilseite.
- 24VAC -> 5V Buck versorgt das Relaisboard auf der Relaisspulenseite.
- Der ESP32-S3 bleibt mit stabilen 5V/VIN bzw. seiner bisherigen Versorgung
  versorgt.
- ESP-GND und Relais-Logik-GND muessen verbunden sein, wenn die Relais-Inputs
  direkt vom ESP getrieben werden.
- 24VAC-Seite bleibt galvanisch von ESP-GPIO getrennt und laeuft nur ueber die
  Relaiskontakte.

Hinweis zum typischen 8-Relaisboard mit `VCC`, `GND`, `JD-VCC`:

- `JD-VCC` versorgt die Relaisspulen mit 5V.
- `VCC` versorgt die Eingangs-/Optokopplerlogik.
- Viele Boards sind active-low: `INx = LOW` schaltet Relais ein.
- Vor finaler Firmware muss am konkreten Board getestet werden:
  - active-low oder active-high
  - ob 3.3V GPIO-Pegel sicher erkannt wird
  - ob `JD-VCC`/`VCC` Jumper getrennt oder gesteckt bleiben soll

### 24VAC-Verdrahtung

Empfohlene Verdrahtung je Zone:

- Eine 24VAC-Trafoleitung als gemeinsamer Common direkt zu allen Ventilspulen.
- Die zweite 24VAC-Trafoleitung auf die COM-Klemme jedes genutzten Relais.
- NO-Klemme des Relais zur jeweiligen Ventilspule.
- NC bleibt unbenutzt.

Damit ist ein Ventil stromlos geschlossen und nur bei angezogenem Relais offen.

## Pinlayout

Minimaler Verdrahtungsaufwand: Die bisherigen acht H-Bruecken-GPIOs werden
direkt als acht Relais-Eingaenge weiterverwendet. Am ESP-Stecker koennen damit
die vorhandenen Leitungen weiter genutzt werden; nur die Gegenseite wandert vom
DRV8871-Board zum Relaisboard.

Aktueller lokaler Pinstand aus `include/config.h`:

| Funktion alt | GPIO | Funktion neu | Relaisboard |
| --- | ---: | --- | --- |
| V1_IN1 | GPIO4 | Zone 1 Relais | IN1 |
| V1_IN2 | GPIO5 | Zone 2 Relais | IN2 |
| V2_IN1 | GPIO6 | Zone 3 Relais | IN3 |
| V2_IN2 | GPIO7 | Zone 4 Relais | IN4 |
| V3_IN1 | GPIO15 | Zone 5 Relais | IN5 |
| V3_IN2 | GPIO16 | Zone 6 Relais | IN6 |
| V4_IN1 | GPIO17 | Reserve 7 | IN7 |
| V4_IN2 | GPIO18 | Reserve 8 | IN8 |

Damit muessen fuer die ersten sechs Zonen sechs GPIO-Leitungen aktiv genutzt
werden. Die restlichen zwei Relais bleiben fuer spaetere Erweiterung oder
Master-Valve/Pumpe frei.

Nicht beruehren:

| Funktion | GPIO |
| --- | ---: |
| RTC SDA | GPIO1 |
| RTC SCL | GPIO2 |
| Touch CS | GPIO8 |
| TFT RST | GPIO21 |
| TFT MOSI | GPIO38 |
| TFT MISO | GPIO39 |
| TFT SCLK | GPIO40 |
| TFT CS | GPIO41 |
| TFT DC | GPIO42 |
| TFT Backlight | GPIO47 |

### Alternative Pinbelegung bei 8 Zonen

Falls direkt 8 Zonen genutzt werden sollen, kann dieselbe Tabelle 1:1 als
Zone 1-8 verwendet werden:

| Zone | GPIO | Relaisboard |
| ---: | ---: | --- |
| 1 | GPIO4 | IN1 |
| 2 | GPIO5 | IN2 |
| 3 | GPIO6 | IN3 |
| 4 | GPIO7 | IN4 |
| 5 | GPIO15 | IN5 |
| 6 | GPIO16 | IN6 |
| 7 | GPIO17 | IN7 |
| 8 | GPIO18 | IN8 |

Fuer den ersten Umbau bleibt die Firmware bei 6 Zonen plus 2 Reserve-Relais,
damit Firmware und Backend nicht gleichzeitig auf 8 Zonen erweitert werden
muessen.

## Firmware-Umbau

### Ventiltreiber

`valve_driver` wird von bistabiler Pulstechnik auf monostabile Relaislogik
umgebaut.

Entfaellt:

- Open-/Close-Pulsdauer
- PWM-Close
- Direct-Close-Test
- RTC-Open-Mask fuer bistabile Ventile
- `closeAll()`-Sonderlogik mit Pulsen
- Ventilsetup-Seite fuer Open/Close/Duty/Pause

Neu:

- `RELAY_ACTIVE_LOW` Konfiguration.
- `RELAY_ZONE_COUNT`, aktuell 6.
- `RELAY_PINS[] = {4, 5, 6, 7, 15, 16}`.
- Beim Boot alle Relais auf aus setzen.
- `open(zone, maxDurationMs)`:
  - prueft Zone und Lockout.
  - blockiert, wenn bereits eine andere Zone offen ist.
  - setzt genau ein Relais aktiv.
  - merkt `openedAt` und `maxMs`.
- `close(zone)`:
  - setzt Relais aus.
  - raeumt State.
- `closeAll()`:
  - setzt alle genutzten Relais aus.
  - ist wieder idempotent und immer sicher.

Wichtig: Relaisausgaenge muessen beim Boot sofort in den inaktiven Zustand
gesetzt werden. Bei active-low Boards bedeutet das:

```cpp
digitalWrite(pin, HIGH);
pinMode(pin, OUTPUT);
```

Danach erst bei Bedarf auf LOW ziehen.

### Scheduler und UI

Die bestehende Logik kann groesstenteils bleiben:

- Maximal eine offene Zone bleibt erhalten.
- Queue fuer gleichzeitige Programme bleibt erhalten.
- Manuelle Steuerung bleibt erhalten.
- `closeAll()` kann wieder als echte Not-Aus-Funktion funktionieren.
- Restzeit sollte langfristig nicht aus Eventlog abgeleitet werden, sondern aus
  ESP-Status/MQTT-State.

Anzupassen:

- Ventilsetup-Seite entfernen oder ersetzen durch Relaisdiagnose:
  - Relais 1-8 anzeigen.
  - Testbutton pro Relais optional nur fuer Wartungsmodus.
- Texte von `Ventilsetup` auf `Relais-Test`/`Ausgaenge` umstellen.
- Statuskarte weiterhin `V1 On | V2 Off ...`.

## MQTT-Zielarchitektur

MQTT ersetzt die zyklische HTTP-Command-/Statuskommunikation. HTTP kann fuer
OTA, initiale Konfiguration oder Diagnose optional erhalten bleiben, sollte aber
nicht mehr im zeitkritischen Loop blockieren.

### Broker

Ist-Stand Infrastruktur:

- Bestehender Mosquitto laeuft in einem anderen Docker-Projekt.
- Extern erreichbar ueber `tofu.creano.de:1883`.
- Traefik leitet TCP ueber den EntryPoint `mqtt` an den Service `mqtt:1883`
  weiter (`HostSNI(*)`, kein TLS auf MQTT-Ebene).
- Der Mosquitto-Service erzeugt Passwortdatei und ACL beim Containerstart aus
  `.env`-Variablen.
- Aktuelles Projekt nutzt bereits User/Pass/ACL fuer `catfeeder/#`.

MQTT wird fuer Bewaesserung erst umgesetzt, wenn Infrastruktur, Credentials,
ACL und externe Verbindung sauber getestet sind. Bis dahin keine Aenderungen an
Web-App oder ESP.

Empfehlung fuer dieselbe Mosquitto-Instanz:

- Bestehenden Broker weiterverwenden, aber Bewaesserung strikt ueber eigenes
  Topic-Prefix und eigene MQTT-User trennen.
- Kein Mischbetrieb unter `catfeeder/#`.
- TLS spaeter nur wenn wirklich benoetigt; zuerst stabile TCP/Auth/ACL testen.
- HTTP bleibt bis nach mehrtaegigem stabilen MQTT-Betrieb aktiv.

Vorgeschlagene `.env`-Erweiterung im bestehenden Mosquitto-Projekt:

```env
MQTT_IRRIGATION_DEVICE_USER=irrigation_esp
MQTT_IRRIGATION_DEVICE_PASS=<strong-password>
MQTT_IRRIGATION_BACKEND_USER=irrigation_backend
MQTT_IRRIGATION_BACKEND_PASS=<strong-password>
MQTT_IRRIGATION_DEVICE_ID=esp32-01
MQTT_IRRIGATION_TOPIC_PREFIX=irrigation
MQTT_PUBLIC_HOST=tofu.creano.de
MQTT_PUBLIC_PORT=1883
```

Vorgeschlagene ACL-Erweiterung:

```text
user irrigation_esp
topic readwrite irrigation/esp32-01/status
topic readwrite irrigation/esp32-01/availability
topic readwrite irrigation/esp32-01/events
topic readwrite irrigation/esp32-01/sensors
topic read irrigation/esp32-01/config
topic read irrigation/esp32-01/commands
topic write irrigation/esp32-01/commands/+/result

user irrigation_backend
topic readwrite irrigation/esp32-01/#
```

Hinweis: Der bestehende `docker-compose.yaml` baut ACL aktuell per `printf`
direkt im `command`-Block. Fuer Bewaesserung entweder diesen Block um die
beiden neuen User erweitern oder langfristig auf eine generierte ACL-Datei
auslagern, damit Catfeeder und Bewaesserung nicht in einer unlesbaren
Einzeile vermischt werden.

Vorbereitungs-Todos Infrastruktur:

- [x] `.env` im bestehenden Mosquitto-Projekt um Bewaesserungs-User/Pass und
  Device-ID erweitern.
- [x] Mosquitto-Startscript/Compose-Command um die neuen User und ACL-Regeln
  erweitern.
- [x] Sicherstellen, dass Traefik den EntryPoint `mqtt` auf Port `1883`
  wirklich nach extern veroeffentlicht.
- [x] Container neu starten und pruefen, dass `mosquitto_passwords` und
  `mosquitto_acl` die neuen User enthalten.
- [x] Von lokal extern gegen `tofu.creano.de:1883` mit den neuen Credentials
  testen.
- [x] Testprotokoll in dieser Doku ergaenzen, bevor Web-App/ESP-Code angepasst
  wird.

Manuelle Infrastrukturtests:

```bash
mosquitto_sub -h tofu.creano.de -p 1883 \
  -u "$MQTT_IRRIGATION_BACKEND_USER" -P "$MQTT_IRRIGATION_BACKEND_PASS" \
  -t 'irrigation/esp32-01/#' -v

mosquitto_pub -h tofu.creano.de -p 1883 \
  -u "$MQTT_IRRIGATION_DEVICE_USER" -P "$MQTT_IRRIGATION_DEVICE_PASS" \
  -t 'irrigation/esp32-01/status' \
  -m '{"test":true,"source":"manual"}'

mosquitto_pub -h tofu.creano.de -p 1883 \
  -u "$MQTT_IRRIGATION_BACKEND_USER" -P "$MQTT_IRRIGATION_BACKEND_PASS" \
  -t 'irrigation/esp32-01/config' -r \
  -m '{"test":true,"retained":true}'

mosquitto_sub -h tofu.creano.de -p 1883 \
  -u "$MQTT_IRRIGATION_DEVICE_USER" -P "$MQTT_IRRIGATION_DEVICE_PASS" \
  -t 'irrigation/esp32-01/config' -C 1 -v
```

Negative ACL-Tests:

```bash
# Device darf nicht in Catfeeder schreiben.
mosquitto_pub -h tofu.creano.de -p 1883 \
  -u "$MQTT_IRRIGATION_DEVICE_USER" -P "$MQTT_IRRIGATION_DEVICE_PASS" \
  -t 'catfeeder/test/status' -m 'forbidden'

# Catfeeder-User duerfen nicht in Bewaesserung schreiben.
mosquitto_pub -h tofu.creano.de -p 1883 \
  -u "$MQTT_DEVICE_USER" -P "$MQTT_DEVICE_PASS" \
  -t 'irrigation/esp32-01/status' -m 'forbidden'
```

Abnahmekriterien vor Code-Aenderungen:

- [x] Publish/Subscribe mit `irrigation_backend` funktioniert fuer
  `irrigation/esp32-01/#`.
- [x] `irrigation_esp` kann Status/Event/Sensor schreiben.
- [x] `irrigation_esp` kann retained Config lesen.
- [x] `irrigation_esp` kann Command-Result schreiben.
- [x] `irrigation_esp` kann keine fremden Prefixe schreiben.
- [x] Catfeeder-User koennen keine Bewaesserungs-Topics schreiben.
- [x] Retained Config kann gesetzt, gelesen und geloescht werden.
- [x] Broker-Neustart erhaelt retained Config und Auth/ACL.

Testprotokoll 2026-07-11:

- Mosquitto startet sauber mit Usern `catfeeder`, `catfeeder_cam`,
  `backend`, `irrigation_esp`, `irrigation_backend`.
- `irrigation_backend` kann verbinden und `irrigation/esp32-01/#`
  abonnieren.
- `irrigation_esp` kann verbinden und auf `irrigation/esp32-01/status`
  publishen.
- `irrigation_backend` kann retained Config auf `irrigation/esp32-01/config`
  schreiben; `irrigation_esp` liest diese retained Config erfolgreich:
  `{"test":true,"retained":true,"source":"manual"}`.
- `irrigation_esp` kann Command-Result auf
  `irrigation/esp32-01/commands/test-manual-001/result` schreiben;
  `irrigation_backend` empfaengt
  `{"id":"test-manual-001","ok":true,"result":"manual-test","zoneNumber":0}`.
- Retained Config Setzen/Lesen/Loeschen erfolgreich:
  - Lesen nach Setzen lieferte
    `{"test":true,"retained":true,"version":"manual-retain-001"}`.
  - Nach retained Delete kam keine retained Message mehr.
- Broker-Neustart erfolgreich:
  - Mosquitto startet ohne Passwort-/ACL-Fehler.
  - User `catfeeder`, `catfeeder_cam`, `backend`, `irrigation_esp`,
    `irrigation_backend` werden angelegt.
  - Retained Config blieb nach Neustart erhalten:
    `{"test":true,"retained":true,"version":"after-broker-restart"}`.
  - Backend verbindet nach Neustart wieder als User `backend`.
- ACL-Trennung erfolgreich:
  - `irrigation_esp` schreibt nicht nach `catfeeder/#`.
  - Catfeeder-User schreiben nicht nach `irrigation/esp32-01/#`.
- Mosquitto-Log zeigte erfolgreiche Verbindungen fuer
  `irrigation_backend` und `irrigation_esp`; verbotene Nachrichten wurden in
  den falschen Subscribern nicht ausgeliefert.

### Topics

Basis-Prefix:

```text
irrigation/<deviceId>/
```

Empfohlene Topics:

| Richtung | Topic | Payload |
| --- | --- | --- |
| ESP -> Broker | `irrigation/esp32-01/status` | retained JSON |
| ESP -> Broker | `irrigation/esp32-01/availability` | `online`/`offline`, retained/LWT |
| ESP -> Broker | `irrigation/esp32-01/events` | Event JSON |
| ESP -> Broker | `irrigation/esp32-01/sensors` | Sensor JSON |
| Backend -> ESP | `irrigation/esp32-01/commands` | Command JSON |
| Backend -> ESP | `irrigation/esp32-01/config` | retained Config JSON |
| ESP -> Broker | `irrigation/esp32-01/commands/<id>/result` | Result JSON |

Status-Payload:

```json
{
  "firmwareVersion": "2.3.0",
  "uptimeSec": 12345,
  "wifiRssi": -63,
  "freeHeap": 250000,
  "valves": [
    {"zone": 1, "open": true, "remainingSec": 812},
    {"zone": 2, "open": false, "remainingSec": 0}
  ],
  "ecowittOk": true,
  "backendOk": true,
  "resetReason": "software"
}
```

Command-Payload:

```json
{
  "id": "cmd-uuid",
  "command": "open",
  "zoneNumber": 1,
  "durationMin": 30
}
```

Unterstuetzte Commands:

- `open`
- `close`
- `close_all`
- `reload_config`
- optional spaeter `relay_test`

Command-Result:

```json
{
  "id": "cmd-uuid",
  "ok": true,
  "result": "opened",
  "zoneNumber": 1,
  "ts": "2026-06-17T12:00:00Z"
}
```

### MQTT-Verhalten am ESP

- Reconnect nicht blockierend.
- Last Will:
  - Topic: `irrigation/esp32-01/availability`
  - Payload: `offline`
  - retained: true
- Nach Connect:
  - `availability=online` retained publishen.
  - Config retained abonnieren.
  - Commands abonnieren.
  - Status sofort publishen.
- Status bei Aenderung sofort publishen, zusaetzlich Heartbeat alle 30-60s.
- Commands sofort ack/result publishen.
- Keine langen HTTP-Calls mehr waehrend Ventil offen ist.

## Backend-Umbau

Backend wird MQTT-Bridge:

- Subscribed auf:
  - `status`
  - `events`
  - `sensors`
  - `commands/+/result`
- Published:
  - retained `config`
  - `commands`

Frontend spricht weiter mit Backend. Das Backend aktualisiert DB/Live-State aus
MQTT. Dadurch muss der ESP keine HTTP-Polls fuer Commands mehr machen.

## Migrationsplan

### Phase 1: Hardware sicher umbauen

- [x] H-Bruecken abklemmen.
- [x] Relaisboard mit 5V versorgen; `VCC` und `JD-VCC` korrekt speisen.
- [x] ESP-GND mit Relais-GND verbinden.
- [x] GPIO4/5/6/7/15/16 auf IN1-IN6 legen.
- [x] 24VAC Common zu allen Hunter-Spulen.
- [x] 24VAC geschaltet ueber Relais COM -> NO -> jeweilige Spule.
- [x] Relais-Active-Level mit Testfirmware und Multimeter pruefen.

### Phase 2: Firmware Relais-Treiber

- [x] `valve_driver` auf Relaislogik umbauen.
- [x] Firmware auf 6 Zonen erweitern.
- [x] Build/USB-Test ohne Ventile:
  - Boot: alle Relais aus.
  - IN1-IN6 schalten nacheinander.
  - COM/NO schliesst bei aktivem Relais.
- [x] Ventilsetup-Seite durch `Ausgaenge` ersetzt:
  - zeigt IN1-IN6 mit GPIO und Zustand.
  - bietet `ALLE AUS`.
- [x] TFT-Dashboard und TFT-Manuell zeigen Zone 5/6 auch dann, wenn die
  Backend-Config noch nur vier Zonen liefert.
- [x] Web-Backend/Web-Frontend im lokalen `jninty-de`-Clone auf 6 Zonen
  erweitert:
  - Default-Zonen 1-6 werden angelegt; bestehende 4-Zonen-Installationen
    bekommen Zone 5/6 nachgezogen.
  - Dashboard, manuelle Steuerung, Statuskarte und History-Legende verwenden
    sechs Zonen.
- [x] Mit 24VAC/Ventilen testen.

### Phase 2b: Sensoren und Zeitsteuerung produktiv absichern

- [x] Backend-Zonen 1-4 auf WH52/GW1200 Channel 1-4 gesetzt und produktiv
  geprueft.
- [x] Backend-Zonen 5/6 auf denselben WH52/GW1200 Channel 5 gesetzt und
  produktiv geprueft.
- [x] Firmware-Sensorchecks wieder aktivieren
  (`SCHEDULER_IGNORE_SENSOR_CHECKS = 0`).
- [x] GW1200-Parser fuer echtes `ch_ec`-JSON erweitern.
- [x] Firmware per OTA auf `192.168.10.116` flashen.
- [x] Manuelle Close-Dauer lokal korrigieren; keine Uptime mehr als
  Event-Dauer loggen.
- [x] Fix fuer manuelle Close-Dauer per OTA auf `192.168.10.116` ausgerollt
  (2026-07-11 09:52 CEST).
- [x] Manuellen Kurzlauf nach OTA getestet: Zone oeffnen/stoppen ok,
  Event-Dauer zeigt keine Uptime-/`1145x min`-Werte mehr.
- [x] Frontend: History-Graphen fuer echte Sensor-Channels dynamisch anzeigen;
  Channel 5 als gemeinsamer Indikator fuer Zone 5/6 kennzeichnen. Umsetzung in
  `jninty-de` gepusht: `c92907f` / Version `1.9.6`.
- [x] Frontend: History-Legende/Graph dynamisch aus echten Sensor-Channels
  ableiten; keinen Phantom-Channel 6 anzeigen, wenn V6 auf Channel 5 liegt.
- [x] Backend/Produktion: Version `1.9.6` ist online; 7-Tage-History liefert
  echte Daten und zeigt keinen Channel 6.
- [x] Backend/Produktion: V5 und V6 sind beide auf WH52/GW1200 Channel 5
  gemappt.
- [x] ESP/TFT: Sensor-History-Chart entfernt; der Sensor-Tab zeigt nur noch
  Live-Werte. Langfristige Auswertung passiert in der App. Per OTA auf
  `192.168.10.116` ausgerollt am 2026-07-11 10:18 CEST.
- [x] Firmware/Web-App: Runtime-State und Queue sichtbar gemacht
  (`idle`/`running`/`queued`, `remainingSec`, `queueLength`). Firmware per OTA
  auf `192.168.10.116` ausgerollt am 2026-07-11 10:30 CEST; Web-App auf
  Version `1.9.7` vorbereitet.
- [x] Web-App/Firmware: Run-Once/Testprogramm umgesetzt. Quick-Test startet
  alle aktiven Zonen nacheinander mit 10/20/30 Sekunden ueber `run_once`.
  Firmware per OTA auf `192.168.10.116` ausgerollt am 2026-07-11 10:48 CEST;
  Web-App auf Version `1.9.8` vorbereitet.
- [x] Web-App: Program Preview umgesetzt. Die Programme-Seite zeigt die
  naechsten 7 Tage mit Lauf/Skip-Entscheidung, Dauer und Grund.
- [x] Web-App/Firmware: Scheduler-Sperre und Rain Delay umgesetzt.
  `/irrigation/control` speichert `controllerEnabled` und `rainDelayUntil` im
  Status-Raw; `/config` liefert diese Flags an den ESP. Firmware `2.2.13`
  skippt geplante Laeufe bei Sperre/Rain Delay.

### Phase 3: OpenSprinkler-inspirierte Runtime-Logik

1. Runtime-State fuer Zonen einfuehren:
   - `idle`
   - `running`
   - `queued`
   - `remainingSec`
   - `queueLength`
   - Status: erledigt mit Firmware `2.2.10`.
2. Queue robuster machen:
   - pro Zone merken, ob sie bereits in der Queue ist.
   - `close(zone)` entfernt laufende und wartende Jobs dieser Zone.
   - `close_all` leert Queue und Runtime-State eindeutig.
   - Status: erledigt mit Firmware `2.2.10`.
3. Manuelle Queue-Strategie bewusst festlegen:
   - Default aktuell: `reject_if_busy`.
   - `append`/`front` erst anbieten, wenn Statusanzeige sauber ist.
4. Betriebsflags vorbereiten:
   - Controller enabled/disabled.
   - Rain delay / Pause.
   - Sensor-/Rain-Ignore pro Zone.
   - Optional Water-Level-Prozent fuer saisonale Laufzeit-Skalierung.
   - Status: Controller enabled/disabled und Rain Delay erledigt mit Firmware
     `2.2.13` und Web-App `1.9.9`.
5. Run-Once/Testprogramm:
   - Status: erledigt mit Firmware `2.2.11` und Web-App `1.9.8`.
   - Quick-Test nutzt `run_once` und die Runtime-Queue.

### Phase 4: MQTT-Infrastruktur vorbereiten

Status: erledigt am 2026-07-11. Broker, Auth/ACL und retained Messages wurden
manuell getestet. Catfeeder- und Bewaesserungs-Topics sind getrennt.

1. Bestehenden Mosquitto unter `tofu.creano.de:1883` fuer Bewaesserung
   vorbereiten:
   - eigene User `irrigation_esp` und `irrigation_backend`.
   - eigenes Prefix `irrigation/esp32-01/#`.
   - Catfeeder-Topics bleiben strikt getrennt.
2. `.env` und Mosquitto-Startscript im bestehenden Docker-Projekt erweitern.
3. Traefik TCP-Route `mqtt` pruefen:
   - Port 1883 extern erreichbar.
   - kein TLS/SNI-Zwang fuer den ersten Schritt.
4. Manuelle Tests mit `mosquitto_pub`/`mosquitto_sub` ausfuehren:
   - positive Publish/Subscribe-Tests.
   - negative ACL-Tests.
   - retained Config setzen/lesen/loeschen.
   - Broker-Neustart erhaelt retained Config sowie Auth/ACL.
5. Credentials abgelegt:
   - ESP: lokal in `firmware/include/config.h`, nicht getrackt.
   - Backend: lokal/produktiv in `.env` bzw. Docker-Environment, nicht
     getrackt.

### Phase 4b: MQTT parallel einfuehren

Status: erledigt und produktiv aktiv.

1. ESP MQTT-Client einbauen:
   - erledigt: `mqtt_transport` mit PubSubClient, Firmware `2.2.14`.
   - OTA auf `192.168.10.116` erfolgreich am 2026-07-11 14:25 CEST.
   - Nachtest: Events werden ab Firmware `2.2.15` direkt bei
     `events::log(...)` auf MQTT publiziert; HTTP-Eventlog bleibt parallel
     gepuffert.
   - nutzt `MQTT_ENABLED`, `MQTT_HOST`, `MQTT_PORT`, `MQTT_DEVICE_ID`,
     `MQTT_TOPIC_PREFIX`, `MQTT_USER`, `MQTT_PASS` aus lokaler `config.h`.
2. Status/Event/Sensor per MQTT publishen:
   - erledigt: `status` retained auf `irrigation/esp32-01/status`.
   - erledigt: `events` auf `irrigation/esp32-01/events`.
   - erledigt: `sensors` auf `irrigation/esp32-01/sensors`.
   - HTTP-POSTs bleiben nur als Fallback aktiv.
   - MQTT-Test erfolgreich:
     - `availability` meldet `online`.
     - `status` meldet Firmware `2.2.14`, IP `192.168.10.116`,
       `valveStates` `000000`, Runtime fuer Zone 1-6 `idle`.
     - `sensors` liefert WH52/GW1200 Channel 1-5.
     - `events` getestet mit manuellem Zone-2-Lauf:
       `open` und `close` kamen sofort per MQTT, Close mit `durationSec`.
3. Backend MQTT-Subscriber schreibt Live-State/Events/Sensoren:
   - erledigt hinter `IRRIGATION_MQTT_ENABLED`.
   - bei `IRRIGATION_MQTT_ENABLED=false` startet kein MQTT-Subscriber.
   - nutzt denselben DB-Ingest wie die bestehenden Device-HTTP-Routen.
4. Produktivstand:
   - Backend-Flag ist produktiv aktiv.
   - App aktualisiert Status/Eventlog/History ueber MQTT-Ingest.
   - HTTP wird nur noch als Fallback genutzt, wenn MQTT nicht verfuegbar ist.

### Phase 5: Commands auf MQTT umstellen

Status: produktiv getestet, HTTP bleibt Fallback.

1. Backend published Commands auf MQTT:
   - umgesetzt in Web-App/Backend `1.9.12`.
   - MQTT-Service publiziert frische pending Commands auf
     `irrigation/esp32-01/commands/<id>`.
   - Pending Commands werden bis zum `acked`-Result periodisch wiederholt.
2. ESP subscribed Commands:
   - umgesetzt in Firmware `2.2.17`.
   - ESP subscribed `irrigation/esp32-01/commands/+`.
   - Empfangene Commands werden in die bestehende `cfg::commands` Queue gelegt.
3. ESP published Result:
   - ESP publiziert sofort `acked` auf
     `irrigation/esp32-01/commands/<id>/result`, damit HTTP-Polling denselben
     Command nicht zusaetzlich als pending abholt.
   - Nach Ausfuehrung publiziert ESP `done` auf demselben Result-Topic.
4. Frontend nutzt Backend weiter unveraendert.
5. HTTP `GET /commands` bleibt vorerst als Fallback aktiv.
6. Teststand 2026-07-11:
   - Backend `1.9.12` deployen.
   - Firmware `2.2.17` per OTA flashen.
   - Vor dem Command-Test Mosquitto-ACL pruefen:
     - `irrigation_backend` muss auf `irrigation/esp32-01/commands/#`
       schreiben duerfen.
     - `irrigation_esp` muss `irrigation/esp32-01/commands/#` lesen duerfen.
     - `irrigation_esp` muss
       `irrigation/esp32-01/commands/+/result` schreiben duerfen.
   - Manuellen Kurzlauf aus der Web-App starten.
   - MQTT pruefen: `commands/<id>`, danach `commands/<id>/result` mit
     `acked` und `done`.
   - Eventlog pruefen: keine Doppel-Events.
   - ACL nachgezogen und getestet:
     - `close_all` auf `commands/codex-close-all-002` wurde vom ESP empfangen.
     - Result-Topic lieferte `acked` und `done`.
   - Nachtest ergab einen WDT-Hinweis bei `mqtt:connect`; Firmware `2.2.17`
     begrenzt den MQTT-Socket-Timeout auf 3 Sekunden.
   - Firmware `2.2.17` ist OTA online:
     - MQTT-Status meldet `firmwareVersion: 2.2.17`, `resetReason: software`
       und `valveStates: 000000`.
     - `close_all` auf `commands/codex-close-all-003` lieferte `acked` und
       `done`.
   - Produktivtest mit Web-App `1.9.12`:
     - Manueller Lauf V2 wurde per MQTT ausgefuehrt.
     - Result-Topic lieferte `done`.
     - Eventlog zeigt genau ein Oeffnen und ein Schliessen ohne Duplikate.

### Phase 6: HTTP reduzieren

Status: produktiv umgesetzt mit Backend `1.9.14` und Firmware `2.2.20`.

1. Commands:
   - MQTT ist primaerer Command-Weg.
   - HTTP `GET /commands` bleibt als Safety-Net aktiv, wird aber nur genutzt,
     wenn die lokale Command-Queue leer ist.
   - Commands merken ihre Quelle:
     - MQTT-Commands werden per MQTT-Result bestaetigt.
     - HTTP-Fallback-Commands werden per HTTP bestaetigt.
     - Wenn das MQTT-`done` fehlschlaegt, versucht die Firmware fuer Backend-
       Commands zusaetzlich HTTP `done`.
2. Tests:
   - Firmware `2.2.18` bauen und OTA flashen. Erledigt am 2026-07-11.
   - Mit Web-App `1.9.12` Kurzlauf starten.
   - MQTT pruefen: Command kommt, Result liefert `acked` und `done`.
   - Im Backend/Eventlog pruefen: keine Duplikate.
   - MQTT kurz trennen oder Broker stoppen und pruefen, dass HTTP-Fallback fuer
     Commands weiterhin funktioniert.
   - CLI-Test nach OTA:
     - MQTT-Status meldet `firmwareVersion: 2.2.18`, `resetReason: software`
       und `valveStates: 000000`.
     - `close_all` auf `commands/codex-close-all-004` lieferte `acked` und
       `done`.
   - Produktivtest Web-App `1.9.12` mit Firmware `2.2.18`:
     - Scheduler laeuft sauber.
     - Manuelle Web-App-Commands kommen nicht beim ESP an, wenn der Backend-
       MQTT-Publisher keinen Command publiziert.
     - Konsequenz fuer `2.2.19`: HTTP-Command-Fallback wieder aktiv lassen,
       aber nur bei leerer lokaler Queue, damit MQTT-Commands nicht
       ueberschrieben werden.
   - Firmware `2.2.19` per OTA geflasht und produktiv getestet:
     - MQTT-Live-Status meldet `firmwareVersion: 2.2.19`,
       `resetReason: software` und `valveStates: 000000`.
     - Manueller Lauf V2 aus Web-App `1.9.12` wurde ausgefuehrt und sauber
       geloggt: Oeffnen 15 min, Schliessen nach manuellem Stop mit 16 s.
3. Config/Telemetry:
   - Backend publiziert Device-Config retained auf
     `irrigation/esp32-01/config`.
   - ESP subscribed retained Config und nutzt HTTP `/config` nur noch als
     Fallback, solange keine MQTT-Config angekommen ist.
   - Status und Sensoren werden bei erfolgreichem MQTT-Publish nicht mehr
     zusaetzlich per HTTP gepostet.
   - Eventlog-HTTP-Flush wird bei bestehender MQTT-Verbindung geleert, weil
     Events bereits beim Entstehen per MQTT publiziert werden.
   - HTTP-Commands bleiben vorerst als Safety-Net aktiv, bis der manuelle
     Command-Pfad ueber MQTT laenger stabil beobachtet wurde.
   - Backend `1.9.14` deployed:
     - Compose liest Bewaesserungs-/MQTT-Settings aus `.env`.
     - Retained Config auf `irrigation/esp32-01/config` ist vorhanden und
       enthaelt echte `zones`, `schedules` und `control`.
   - Firmware `2.2.20` laeuft stabil:
     - MQTT-Live-Status meldet `firmwareVersion: 2.2.20`,
       `valveStates: 000000` und leere Queue.
     - MQTT-Command `close_all` auf `commands/codex-close-all-005` lieferte
       `acked` und `done`.

## Offene Entscheidungen

- TLS fuer MQTT:
  - aktuell bewusst nicht umgesetzt.
  - Broker laeuft ueber TCP mit getrennten Usern, Passwort und ACL.
  - TLS spaeter pruefen, wenn externer Zugriff oder hoehere Security-Anforderung
    wichtiger wird.
- HTTP-Command-Fallback:
  - `/commands` bleibt aktiv, aber nur als Safety-Net.
  - Entfernen erst nach mehreren stabilen Tagen mit manuellen MQTT-Commands.

## Empfehlung

Erreichter stabiler Zielstand:

- 6 Hunter PVG-101 an Relais 1-6.
- Relais 7-8 Reserve.
- GPIO4/5/6/7/15/16 fuer Zone 1-6.
- Display, RTC, Ecowitt und lokale Schedulerlogik bleiben.
- MQTT fuer Config, Status, Events, Sensoren und Commands.
- HTTP nur noch als Fallback, insbesondere fuer Commands.
