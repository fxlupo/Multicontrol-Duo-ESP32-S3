# ESP stability notes

Stand: 2026-06-14

## Merker fuer die naechste Session

Aktueller Arbeitsstand zuletzt gebaut am 2026-06-14:

- Safety-/Command-Patch umgesetzt und lokal erfolgreich gebaut:
  - `close_all` raeumt jetzt auch Scheduler-Queue und aktuellen Job ab, damit
    nach `ALLE STOP` keine wartende Zone wieder startet.
  - Manuelles `close` loggt die echte Ventil-Laufzeit ueber
    `valve::openDurationSec()`, nicht mehr die ESP-Uptime.
  - Manuelles `open` auf eine bereits offene Zone wird als `Schon offen`
    behandelt und als `already open` quittiert, ohne neuen Open-Event/Pulse.
  - Fehlgeschlagene/ungueltige manuelle Commands werden mit `ok=false` und
    passendem Result quittiert.
  - Ecowitt-Polling setzt jetzt `setConnectTimeout(1000)` und Breadcrumb
    `ecowitt:get`.
- Build:
  - `/Users/franzwolf/.platformio/penv/bin/pio run`
  - Ergebnis: `esp32s3 SUCCESS`

Vorheriger Vor-Ort-/Rollback-Stand vom 2026-06-11/12:

- Aktuelle Firmware auf dem ESP ist der Rollback-Stand vom 2026-06-11 ca.
  21:16.
- Enthalten:
  - HTTP-Haertungs-Patch mit zentralem `backend_http`-Helper.
  - Feinere HTTP-Breadcrumbs `http:<stage>:begin/send/body/end`.
  - Status-POST bei offenem Ventil wieder erlaubt, damit Frontend-Status
    funktioniert.
  - Bei offenem Ventil wird weiterhin `GET /commands` gepollt, damit
    Frontend-Stop-Befehle abgeholt werden.
  - Boot-/Crash-Events werden als System/Reset klassifiziert.
- Nicht enthalten:
  - `forceClose()` wurde wieder entfernt.
  - Ecowitt-Outdoor-Werte `outTempC`/`outHumidity` sind noch nicht in der
    Firmware.
  - Grosser HTTP-Network-Task/MQTT-Umbau ist noch nicht gemacht.
- Backend-Zwischenfall vom 2026-06-11 ist geloest:
  - Ursache war fehlende DB-Migration fuer `out_temp_c`/`out_humidity`.
  - Nach `drizzle-kit migrate` liefert Dashboard wieder 200 und
    `POST /device/status` wieder 200.
- Letzter bekannter Firmware-WDT vor dem Rollback:
  - 2026-06-06 17:58
  - `R task_wdt B286 C279 U16203 S http:config:send H259776`
  - Danach bis 2026-06-11 stabile Scheduler-Laeufe beobachtet.

Naechste geplante Reihenfolge ab 2026-06-14:

1. Safety-/Command-Patch per OTA flashen und direkt testen:
   - `ALLE STOP` waehrend laufender/queued Bewaesserung darf keine Zone wieder
     starten.
   - Frontend-Stop einer laufenden Zone loggt plausible Laufzeit.
   - Erneutes Frontend-Open auf offene Zone meldet `already open`/`Schon offen`.
   - Blockierte Opens werden im Backend nicht mehr als erfolgreich ausgefuehrt
     gemeldet.
2. Danach weiter beobachten:
   - TFT-Steuerung muss wie vorher funktionieren.
   - Frontend-Status/Commands muessen funktionieren.
   - Eventlog auf neue `task_wdt` pruefen, besonders `http:*:send` und
     `ecowitt:get`.
3. Falls wieder `http:*:send`-WDTs auftreten:
   - Nicht nur WDT erhoehen.
   - Zuerst DNS-Hypothese testen: Backend testweise per IP statt Hostname oder
     DNS-Zeit separat instrumentieren.
   - Danach je nach Ergebnis IP-/DNS-Cache, Network-Task oder spaeter MQTT.
4. Force-Close-Thema nicht erneut schnell patchen:
   - Der Versuch am 2026-06-11 wurde wegen Schaltproblemen zurueckgerollt.
   - Vor neuem Ansatz erst Ventillogik/DRV-Pulse und UI-Interaktion sauber
     analysieren.
   - Bis dahin bei unklarem physischem Zustand `ALLE STOP` am TFT verwenden.
5. Ecowitt-Outdoor-Werte erst vor Ort mit echten GW1200-Daten:
   - echtes `/get_livedata_info` sichern.
   - danach Parser fuer `outTempC`/`outHumidity` bauen.
   - Backend/Frontend sind dafuer bereits vorbereitet.
6. MQTT bleibt eine spaetere Architektur-Option, aber aktueller Fokus bleibt:
   HTTP stabilisieren und nur gezielt umbauen.

## Aktueller Firmware-Stand

- GitHub `main`: `806fe8b docs: capture stability follow-up plan`
- Lokal: `309b9a0 Stabilize config sync against WDT resets` plus
  HTTP-Haertungs-Patch vom 2026-06-03/04.
- Firmware-Version: `2.2.9`
- OTA zuletzt erfolgreich auf `192.168.10.116` am 2026-06-11 um ca. 21:16
- Naechster geplanter Vor-Ort-Patch am 2026-06-11:
  - Force-Close fuer einzelne Frontend-Stop-Kommandos.
  - Ecowitt-Outdoor-Werte bleiben verschoben, bis Sensoren/GW1200-Daten komplett
    vor Ort geprueft werden koennen.
- `include/config.h` ist lokal/ignoriert und wird nicht committed.

## Eingebaute Schutzmassnahmen

- `stability`-Modul schreibt Boot-/Crash-Zaehler und Resetgrund.
- Boot-Event im Backend-Eventlog enthaelt Breadcrumb:
  - `R`: Resetgrund
  - `B`: Boot-Zaehler
  - `C`: Crash-Zaehler
  - `U`: letzte NVS-Uptime-Schaetzung
  - `S`: letzter Codeabschnitt
  - `H`: Heap
- Nach Watchdog/Crash:
  - Oeffnen fuer `CRASH_OPEN_LOCKOUT_MS` gesperrt.
  - zusaetzliche `closeAll()`-Durchlaeufe.
- `valve::open()` blockiert jetzt hart, wenn bereits eine andere Zone offen ist.

## Beobachtung aus Eventlog

Sehr viele Resets:

```text
R task_wdt ... S loop:config-sync H257xxx
```

Vereinzelt:

```text
S loop:status-post
S loop:ecowitt
```

Heap bleibt stabil um ca. `257k`, daher aktuell kein Hinweis auf Heap-Leak.

## Arbeitshypothese

Der Loop wird von blockierenden HTTP/TLS-Aufrufen aufgehalten, bis der Task-WDT ausloest.

Risikofaktoren im alten Stand:

- `WDT_TIMEOUT_MS = 8000`
- `HTTP_TIMEOUT_MS = 8000`
- `INTERVAL_CONFIG_MS = 5000`
- `cfg::sync()` kann mehrere HTTP-Calls direkt nacheinander machen:
  - `GET /config`
  - `GET /commands`
  - mehrere `POST /commands/:id/ack`
- WDT wird waehrend dieser blockierenden Calls nicht gefuettert.
- Waehrend ein Ventil offen ist, koennen Netzwerk-Syncs weiterhin lange blockieren.

## Patch vom 2026-05-31

Umgesetzt und per OTA auf `192.168.10.116` geflasht:

- `INTERVAL_CONFIG_MS` von `5000` auf `30000` erhoeht.
- `HTTP_TIMEOUT_MS` von `8000` auf `3000` reduziert.
- `cfg::sync()` mit feineren Breadcrumbs instrumentiert:
  - `config:get`
  - `config:parse`
  - `config:commands`
  - `config:ack`
  - `config:save`
- Nach HTTP-Calls in `cfg::sync()` und Command-Acks wird der Task-WDT gefuettert.
- Backend-Backoff eingebaut:
  - Nach 3 Backend-Fehlern wird der Config-Sync fuer `BACKEND_ERROR_BACKOFF_MS` pausiert.
  - Default: `120000` ms.
- Waehrend ein Ventil offen ist, wird `cfg::sync()` uebersprungen. Dadurch werden lange Backend-Calls waehrend aktiver Bewaesserung vermieden.

Hinweis: Zum Zeitpunkt des OTA-Tests waren keine Ventile angeschlossen.

## Beobachtung nach OTA

Stand 2026-06-03: Seit dem Flash/OTA am 31.05. 18:11 lief die Firmware
deutlich stabiler. Die planmaessigen und manuellen Bewaesserungen wurden
ausgefuehrt. Es gab aber am 02.06. 22:06 noch einen einzelnen Watchdog-Reset:

```text
R task_wdt B279 C275 U186902 S config:get H257584
```

Einordnung:

- `U186902` entspricht ca. 51 h 55 min Laufzeit vor dem Reset.
- `S config:get` zeigt weiterhin auf einen blockierenden Backend-GET `/config`.
- Heap blieb mit ca. `257k` stabil; weiterhin kein Hinweis auf Heap-Leak.
- Der Event erschien bisher als `Skip / Alle / System`, war aber ein
  Boot-/Crash-Breadcrumb und kein Bewaesserungs-Skip.

Im Backend-Eventlog weiter beobachten:

- Kommen noch `task_wdt`-Resets?
- Wenn ja, welches genaue `S ...`?
  - `config:get`: Haenger beim Config-GET.
  - `config:commands`: Haenger beim Commands-GET oder JSON-Parsing.
  - `config:ack`: Haenger beim Command-Ack.
  - `loop:status-post`, `loop:ecowitt` oder andere Stages: naechster Kandidat ausserhalb des Config-Syncs.
- Bleibt der Heap weiter stabil?
- Wenn Ventile wieder angeschlossen sind: Passieren Resets waehrend aktiver Bewaesserung?

## Vorbereiteter Patch vom 2026-06-03

Ziel: erster Schritt zur Netzwerk-Haertung, damit beim naechsten Vor-Ort-Termin
nur noch Build/Flash/Beobachtung ansteht.

Umgesetzt:

- Neuer zentraler Backend-HTTP-Helper:
  - `src/backend_http.h`
  - `src/backend_http.cpp`
- Alle Backend-Requests aus `config_sync.cpp` laufen ueber diesen Helper:
  - `GET /config`
  - `GET /commands`
  - Command-Acks
- Status-, Sensor-, Event- und Eventload-Requests aus `event_logger.cpp` laufen
  ebenfalls ueber den Helper.
- Der Helper setzt:
  - `http.setTimeout(HTTP_TIMEOUT_MS)`
  - `http.setConnectTimeout(HTTP_TIMEOUT_MS)`
  - `http.setReuse(false)`
  - frischen Client pro Request
  - WDT-Feed nach Rueckkehr aus dem Request
- Feinere Breadcrumbs fuer Backend-HTTP:
  - `http:<stage>:begin`
  - `http:<stage>:send`
  - `http:<stage>:body`
  - `http:<stage>:end`
- Nichtkritische Backend-Uploads werden waehrend eines offenen Ventils
  uebersprungen und spaeter nachgeholt:
  - `events::flush()`
  - `events::uploadSensors()`
  - `events::loadRecentFromBackend()`
- Follow-up am 2026-06-04: `events::postStatus()` wird wieder auch bei
  offenem Ventil gesendet, weil das Web-Frontend darueber den Live-
  Ventilzustand (`valveStates`) aktualisiert. Damit ist die Anzeige wieder wie
  vor dem HTTP-Haertungs-Patch, waehrend groessere Event-/Sensor-/History-
  Uploads weiter pausieren.
- Follow-up am 2026-06-04: Bei offenem Ventil wird `cfg::sync()` nicht mehr
  komplett uebersprungen. Der grosse `GET /config` bleibt pausiert, aber
  `GET /commands` laeuft weiter, damit Frontend-Stop-Befehle (`close`,
  `close_all`) abgeholt werden. Solange ein Ventil offen ist, wird dieser
  Command-Pfad alle `COMMAND_POLL_OPEN_MS` gepollt, Default `5000` ms.
  Dieser Fix wurde am 2026-06-04 um ca. 16:21 per OTA geflasht.
- Boot-/Crash-Breadcrumb wird kuenftig als `action=reset` mit
  `reason=<resetReasonCode>` geloggt, nicht mehr als `skip/system`.
- Defensiver Fallback: Falls ein aelteres Backend `reset`-Events ablehnt, wird
  der betroffene Event-Batch noch einmal im alten `skip/system`-Format
  gesendet, damit der Eventpuffer nicht blockiert.
- TFT-Eventlog zeigt `reset` als `RESET`.

Nicht in diesem Patch:

- `cfg::sync()` ist noch keine echte State-Machine. Es kann weiterhin mehrere
  Backend-Schritte nacheinander ausfuehren, allerdings mit besserer HTTP-
  Kapselung und ohne Backend-Uploads waehrend offener Ventile.
- Ecowitt/GW1200 nutzt noch seinen eigenen lokalen HTTP-Pfad.
- `WDT_TIMEOUT_MS` bleibt vorerst bei 8000 ms. Eine Erhoehung auf 12000-15000 ms
  bleibt eine Option, falls nach dem HTTP-Patch weiterhin seltene TLS-Haenger
  auftreten.

## OTA vom 2026-06-04

Der vorbereitete HTTP-Haertungs-Patch wurde am 2026-06-04 vor Ort per OTA auf
`192.168.10.116` geflasht.

Upload:

```text
esp32s3_ota SUCCESS
Upload 100%
Result: OK
Zeit: ca. 13:38
```

Direkt danach erschien im Backend-Eventlog:

```text
04.06., 13:38
System / Alle / software
2370 min
R software B280 C275 U142202 S loop:ota H249048
```

Einordnung:

- `R software`: erwarteter Software-Neustart durch OTA, kein Crash.
- `S loop:ota`: letzter Breadcrumb lag im OTA-Handling, passt zum Flash.
- `B280`: Boot-Zaehler erhoeht.
- `C275`: Crash-Zaehler unveraendert; der OTA-Neustart hat keinen Crash
  erzeugt.
- `U142202`: vorherige Laufzeit ca. 39 h 30 min.
- `H249048`: Heap weiterhin plausibel/stabil.
- Die neue System-Klassifikation wird vom Backend akzeptiert. Der Event
  erscheint als `System`/`software`, nicht mehr als Bewaesserungs-`Skip`.

## Aktueller Soak-Test

Stand 2026-06-11 nach drittem OTA:

- Firmware lief nach dem OTA vom 2026-06-04 um 16:21 mehrere Tage mit
  geplanten Bewaesserungen.
- Es gab einen kritischen Zeitraum am 2026-06-06 mit mehreren `task_wdt`-
  Resets, danach liefen die Scheduler-Zyklen bis 2026-06-09 wieder sauber.
- Stand 2026-06-11: letzter bekannter Absturz bleibt der Reset vom 2026-06-06
  um 17:58. Danach liefen die Scheduler-Zyklen bis 2026-06-11 stabil.
- Startpunkt fuer die Beobachtung ist der Command-Poll-Fix-OTA vom 2026-06-04
  um ca. 16:21. Die vorherigen OTA-Staende um 13:38 und 14:42 wurden durch
  diesen Follow-up-Flash ersetzt.

Beobachtung 2026-06-09:

```text
06.06. 13:26 R task_wdt B283 C276 U162006 S http:config:send H260176
06.06. 13:27 R task_wdt B284 C277 U162006 S http:config:send H259892
06.06. 13:28 R task_wdt B285 C278 U162006 S http:status:send H258268
06.06. 17:58 R task_wdt B286 C279 U16203  S http:config:send H259776
```

Einordnung:

- Die neuen Breadcrumbs funktionieren und zeigen eindeutig: Der Haenger liegt
  nicht mehr im JSON-Parsing oder Eventlog, sondern waehrend `HTTPClient` im
  Send-Pfad blockiert.
- Betroffen waren `GET /config` und `POST /status`.
- `setTimeout`, `setConnectTimeout` und `setReuse(false)` verhindern diese
  seltenen Send-Blockaden nicht vollstaendig.
- Der Heap bleibt stabil (`H258k` bis `H260k`), weiterhin kein Hinweis auf ein
  Heap-Leak.
- Die Reset-Kaskade am 06.06. 13:26-13:28 deutet auf eine temporaere
  Netzwerk-/TLS-/Backend-Situation hin, die mehrere direkte Neustarts
  verursacht hat.
- Nach dem letzten Reset am 06.06. 17:58 liefen die geplanten Bewaesserungen
  am 07.06., 08.06. und 09.06. sauber weiter.

Naechster technischer Schluss:

- Backoff nach HTTP-Fehlern reicht nicht, weil der WDT zuschlaegt, bevor der
  HTTP-Call mit einem Fehler zurueckkommt.
- Nur `WDT_TIMEOUT_MS` zu erhoehen waere hoechstens ein Pflaster.
- Der naechste robuste Schritt ist, blockierende HTTP-Arbeit aus dem
  zeitkritischen Loop zu entkoppeln oder den HTTP-Send-Pfad so zu kapseln, dass
  ein blockierender Netzwerkaufruf nicht mehr den Ventil-/Scheduler-Loop
  anhaelt.

## Todo nach dem Soak-Test

### Architektur-Option MQTT

MQTT waere langfristig eine passende Architektur fuer Live-Status und
Commands:

- Frontend/Backend koennen Commands sofort an den ESP publishen, statt dass der
  ESP per HTTP pollt.
- ESP kann Ventilstatus, Health, Sensoren und Events als Stream publishen.
- Retained Status, Last-Will/Offline-Erkennung und QoS waeren fuer diesen
  Anwendungsfall hilfreich.
- Das Backend bleibt trotzdem fuer DB, Historie, Auth und Frontend-Anbindung
  notwendig; MQTT waere primaer der Transport fuer Live-Kommunikation.

Entscheidung Stand 2026-06-04:

- MQTT als moegliche groessere v2.3-Architektur im Blick behalten.
- Aktueller Fokus bleibt aber bewusst:
  - HTTP-Haertungs-Patch ein paar Tage stabil laufen lassen.
  - `task_wdt`/Breadcrumbs auswerten.
  - Frontend-Status und Frontend-Stop mit dem aktuellen HTTP-Ansatz
    stabilisieren.
  - Erst nach dem Soak-Test entscheiden, ob weiterer HTTP-Feinschliff reicht
    oder ein MQTT-Umbau geplant wird.

### Explizite Close-Pulse fuer bistabile Ventile

Beobachtung am 2026-06-04 nach OTA: Falls Ventile physisch offen sind, die
Firmware nach Boot/OTA intern aber `closed` annimmt, kann ein Tipp auf eine
einzelne Zonen-Kachel am TFT als Toggle interpretiert werden und dadurch erst
einen `open`-Pulse senden. Sicheres Schliessen funktioniert ueber `ALLE STOP`,
weil dort `closeAll()` unabhaengig vom internen Zustand Close-Pulse sendet.

Nach dem Stabilitaets-Soak pruefen und ggf. patchen:

- Frontend/TFT-Stop fuer eine einzelne Zone sollte einen echten Close-Pulse
  senden koennen, auch wenn `valve::isOpen(zone)` intern `false` ist.
- Moeglicher Ansatz:
  - neue Funktion `valve::forceClose(uint8_t zone)` oder
    `valve::close(zone, force=true)`
  - `close_all` bleibt wie bisher `closeAll()`
  - Frontend-Command `close` nutzt Force-Close
  - TFT-Zonen-Toggle bleibt ggf. Toggle, aber eine explizite Stop-Aktion sollte
    Force-Close verwenden
- Ziel: Physischer Ventilzustand und Firmware-State koennen nach Boot/OTA nicht
  mehr verhindern, dass ein Stop-Befehl einen Close-Pulse ausloest.
- Bis dahin bei unklarem physischem Zustand am TFT bevorzugt `ALLE STOP`
  benutzen.

Patch-Plan 2026-06-11:

- `valve::forceClose(uint8_t zone)` ergaenzen.
- Frontend-Command `close` in `scheduler.cpp` auf `forceClose()` umstellen.
- `close_all` bleibt `closeAll()`.
- Ziel: Ein einzelner Frontend-Stop sendet immer einen Close-Pulse, auch wenn
  der Firmware-State nach Boot/OTA nicht zum physischen bistabilen Ventil passt.
- Umsetzung gebaut und am 2026-06-11 um ca. 20:56 per OTA auf
  `192.168.10.116` geflasht.
- Rollback am 2026-06-11 um ca. 21:16: Force-Close wieder entfernt, weil sich
  die Ventile mit diesem Patch nicht zuverlaessig schalten liessen. Aktuelle
  Firmware-Basis ist wieder der vorherige Stand mit HTTP-Haertung und
  Command-Poll, aber ohne `forceClose()`.

### Ecowitt Outdoor-Werte im Heartbeat

Backend/Frontend sind vorbereitet:

- `irrigation_status` hat `out_temp_c` und `out_humidity`.
- `POST /device/status` nimmt `outTempC` und `outHumidity` entgegen.
- `GET /api/irrigation/weather-snapshot` liefert Aussentemperatur, Feuchte und
  aktuelle Bodensensoren fuer das Dashboard-Widget.

Firmware-Todo fuer den naechsten Vor-Ort-Termin:

1. Direkt am Standort das echte GW1200-JSON sichern:

```bash
curl "http://<ECOWITT_IP>:<ECOWITT_PORT>/get_livedata_info"
```

2. Danach `ecowitt_client.cpp` gezielt um die echten Felder fuer
   Aussentemperatur und Aussenfeuchte erweitern.
3. `events::postStatus()` im Heartbeat um die neuen Felder ergaenzen:

```json
{
  "outTempC": 23.4,
  "outHumidity": 67.0
}
```

4. Bodenfeuchtesensoren nicht umbauen; WH51/WH52 laufen bereits ueber
   `POST /device/sensor-readings` und erscheinen automatisch im Widget.
5. Erst vor Ort implementieren/testen, weil das konkrete Ecowitt-JSON je nach
   Firmware/Gateway-Stand variieren kann.

Backend-Zwischenfall am 2026-06-11:

- Nach Backend-/Frontend-Update zeigte das Dashboard `API Fehler 500` und der
  ESP wurde in der App als offline angezeigt.
- Ursache war nicht die Firmware, sondern eine fehlende DB-Migration:
  - `SqliteError: no such column: "out_temp_c"`
  - `table irrigation_status has no column named out_temp_c`
- Nach Ausfuehren der Migration im Backend-Container liefen wieder:
  - `GET /api/irrigation/dashboard` -> 200
  - `POST /api/irrigation/device/status` -> 200
- Merke: Bei kuenftigen Backend-Aenderungen mit neuen Statusfeldern immer
  pruefen, dass die Migration im laufenden Docker-Volume wirklich angewendet
  wurde, nicht nur das Image neu gebaut ist.

## Flash-/Testplan fuer den naechsten Vor-Ort-Termin

Dieser Abschnitt ist erledigt fuer den OTA vom 2026-06-04. Er bleibt als
Referenz fuer spaetere OTA-Runden stehen.

1. Vor dem Flash kontrollieren:
   - Eventlog sichern.
   - Alle Ventile physisch geschlossen.
   - OTA-Seite am TFT erreichbar oder USB-Fallback bereit.
2. Lokal bauen:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run
```

3. OTA starten:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run -e esp32s3_ota -t upload --upload-port 192.168.10.116
```

4. Direkt nach Boot pruefen:
   - Eventlog enthaelt `RESET`/`reset` statt Bewaesserungs-`Skip`, sofern das
     Backend die neue Action akzeptiert.
   - Falls weiterhin `Skip / System` erscheint, hat der Fallback gegriffen; dann
     Backend/Frontend spaeter fuer `reset` erweitern.
   - Statuskarte zeigt ESP online.
5. Danach mindestens 72 h beobachten:
   - Kommt noch `task_wdt`?
   - Neues `S ...` notieren, speziell `http:config:send`, `http:config:body`,
     `http:status:send`, `http:events-post:send`, `loop:ecowitt`.
   - Heap/RSSI/Backend-Erreichbarkeit mitloggen, wenn verfuegbar.

## Naechste Schritte falls Resets bleiben

1. OTA/USB-Zugang sicherstellen und Serial Monitor oeffnen.
2. Direkt vor Tests kontrollieren, dass alle Ventile physisch geschlossen sind.
3. Den neuen Breadcrumb `S ...` aus dem Boot-Event sichern.
4. Falls der Reset in `http:*:send` oder `http:*:body` liegt, den betroffenen
   Request weiter entkoppeln oder als eigenen Task/State-Machine auslagern.
5. Falls der Reset weiter im Config-Sync liegt:
   - `cfg::sync()` als State-Machine umbauen.
   - Maximal einen Backend-Request pro Loop-Zeitfenster.
   - Command-Acks auf einen Ack pro Durchlauf begrenzen.
6. Falls der Reset in `loop:ecowitt` liegt:
   - Ecowitt eigenen kurzen Timeout geben, z.B. 1000 ms.
   - Breadcrumb `ecowitt:get` setzen.
   - Sensor-Upload nur fuer frische Werte oder mit `ageSec`.
7. Falls das Backend `reset`-Events nicht akzeptiert:
   - Backend/Frontend-Eventmodell um `reset` oder `boot` erweitern.
   - Danach den Firmware-Fallback fuer alte `skip/system`-Bootevents entfernen.
8. Build lokal:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run
```

9. OTA:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run -e esp32s3_ota -t upload --upload-port 192.168.10.116
```

## Wichtig

Nicht produktiv betreiben, solange noch regelmaessige `task_wdt`-Resets auftreten.
Bei bistabilen Ventilen ist ein Reset waehrend aktiver Bewaesserung ein echtes Betriebsrisiko, auch wenn Boot-Failsafes inzwischen verbessert sind.
