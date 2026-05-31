# ESP stability notes

Stand: 2026-05-31

## Aktueller Firmware-Stand

- GitHub `main`: `806fe8b docs: capture stability follow-up plan`
- Firmware-Version: `2.2.9`
- OTA zuletzt erfolgreich auf `192.168.10.116` am 2026-05-31
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

Im Backend-Eventlog beobachten:

- Kommen noch `task_wdt`-Resets?
- Wenn ja, welches genaue `S ...`?
  - `config:get`: Haenger beim Config-GET.
  - `config:commands`: Haenger beim Commands-GET oder JSON-Parsing.
  - `config:ack`: Haenger beim Command-Ack.
  - `loop:status-post`, `loop:ecowitt` oder andere Stages: naechster Kandidat ausserhalb des Config-Syncs.
- Bleibt der Heap weiter stabil?
- Wenn Ventile wieder angeschlossen sind: Passieren Resets waehrend aktiver Bewaesserung?

## Naechste Schritte falls Resets bleiben

1. OTA/USB-Zugang sicherstellen und Serial Monitor oeffnen.
2. Direkt vor Tests kontrollieren, dass alle Ventile physisch geschlossen sind.
3. Den neuen Breadcrumb `S ...` aus dem Boot-Event sichern.
4. Falls der Reset nicht mehr in `config:*` liegt, den betroffenen HTTP-Pfad analog begrenzen oder entkoppeln.
5. Build lokal:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run
```

6. OTA:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run -e esp32s3_ota -t upload --upload-port 192.168.10.116
```

## Wichtig

Nicht produktiv betreiben, solange noch regelmaessige `task_wdt`-Resets auftreten.
Bei bistabilen Ventilen ist ein Reset waehrend aktiver Bewaesserung ein echtes Betriebsrisiko, auch wenn Boot-Failsafes inzwischen verbessert sind.
