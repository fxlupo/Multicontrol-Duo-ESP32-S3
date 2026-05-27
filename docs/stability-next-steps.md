# ESP stability notes

Stand: 2026-05-27

## Aktueller Firmware-Stand

- GitHub `main`: `27869e8 Prepare firmware 2.2.9 stability logging`
- Firmware-Version: `2.2.9`
- OTA zuletzt erfolgreich auf `192.168.10.116`
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

Risikofaktoren im aktuellen Stand:

- `WDT_TIMEOUT_MS = 8000`
- `HTTP_TIMEOUT_MS = 8000`
- `INTERVAL_CONFIG_MS = 5000`
- `cfg::sync()` kann mehrere HTTP-Calls direkt nacheinander machen:
  - `GET /config`
  - `GET /commands`
  - mehrere `POST /commands/:id/ack`
- WDT wird waehrend dieser blockierenden Calls nicht gefuettert.
- Waehrend ein Ventil offen ist, koennen Netzwerk-Syncs weiterhin lange blockieren.

## Naechste Schritte vor Ort

1. OTA/USB-Zugang sicherstellen und Serial Monitor oeffnen.
2. Direkt vor Tests kontrollieren, dass alle Ventile physisch geschlossen sind.
3. Patch vorbereiten:
   - `INTERVAL_CONFIG_MS` auf 30000-60000 ms erhoehen.
   - `HTTP_TIMEOUT_MS` auf 2500-3000 ms reduzieren.
   - `cfg::sync()` mit feineren Breadcrumbs instrumentieren:
     - `config:get`
     - `config:parse`
     - `config:commands`
     - `config:ack`
     - `config:save`
   - Nach jedem HTTP-Call `wdt::feed()`.
   - Backend-Backoff einbauen: bei mehreren Fehlern Sync fuer 2-5 Minuten aussetzen.
   - Waehrend Ventil offen ist: lange Backend-Syncs aussetzen oder stark begrenzen.
4. Build lokal:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run
```

5. OTA:

```bash
/Users/franzwolf/.platformio/penv/bin/pio run -e esp32s3_ota -t upload --upload-port 192.168.10.116
```

6. Nach OTA Eventlog beobachten:
   - Kommen noch `task_wdt`?
   - Wenn ja, welches genaue `S ...`?
   - Passieren Resets waehrend aktiver Bewaesserung?

## Wichtig

Nicht produktiv betreiben, solange noch regelmaessige `task_wdt`-Resets auftreten.
Bei bistabilen Ventilen ist ein Reset waehrend aktiver Bewaesserung ein echtes Betriebsrisiko, auch wenn Boot-Failsafes inzwischen verbessert sind.
