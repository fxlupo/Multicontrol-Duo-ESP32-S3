# OpenSprinkler-App Verbesserungen: Implementierungsplanung

Stand: 2026-07-11

Ziel: Nur die Ideen aus der OpenSprinkler-App uebernehmen, die echten Mehrwert
fuer unsere Bewaesserung bringen. Keine Codeuebernahme aus OpenSprinkler-App,
da AGPL-3.0. Umsetzung nur im Bewaesserungsteil von `jninty-de` und in der
Firmware, wenn Runtime-State benoetigt wird.

## Leitplanken

- In `jninty-de` nur Bewaesserung anfassen:
  - `server/src/routes/irrigation.ts`
  - `server/src/db/schema.ts`, nur wenn fuer Bewaesserung noetig
  - `src/pages/IrrigationPage.tsx`
  - Tests/Doku mit klarem Irrigation-Bezug
- Kein Umbau der restlichen App.
- MQTT bleibt nachrangig.
- ESP/TFT bleibt fuer lokale Bedienung und Live-Werte; Langzeit-History,
  Vorschau und Auswertung gehoeren in die Web-App.
- Vor Ort zuerst Firmware-Fix ausrollen und Produktivverhalten pruefen, danach
  Web-App-Verbesserungen testen.

## Abgleich vor Ort 2026-07-11

Nicht nur der manuelle Close-Dauer-Bug ist offen. Fuer den heutigen
Fertigstellungsstand gehoeren diese Punkte zusammen:

- Firmware: lokaler Fix fuer manuelle Close-Dauer per OTA auf ESP
  `192.168.10.116` ausrollen und testen.
  - Ausgerollt am 2026-07-11 09:52 CEST.
  - Manueller Kurzlauf/Test der Event-Dauer erfolgreich.
- Web-App History: Backend-Limit ist bereits auf langen Verlauf erhoeht, aber
  die UI darf nicht mehr starr 6 Sensorlinien/Legenden anzeigen.
  - Umsetzung als `jninty-de` Version `1.9.6` produktiv online.
- Web-App History: echte Sensor-Channels aus den Daten ableiten, aktuell
  erwartbar Channel 1-5.
  - Geprueft: 7-Tage-History zeigt echte Daten.
- Web-App History: Channel 5 eindeutig als geteilter Indikator fuer V5/V6
  labeln; kein eigener Channel 6, wenn V6 auf Channel 5 gemappt ist.
  - Geprueft: kein Channel 6 in der History.
- Backend-Konfig:
  - Geprueft: V5 und V6 sind beide auf WH52/GW1200 Channel 5 gemappt.
- Web-App Eventlog/History mit weiteren Filtern/Summen/Export wird nicht mehr
  umgesetzt; aktueller Stand reicht produktiv aus.
- ESP/TFT Sensor-History: nicht als heutiges Muss, aber weiterhin geplant:
  entfernen oder auf Live-Werte reduzieren, weil Langzeit-History in die App
  gehoert.

## Prioritaeten

### P0: Morgen vor Ort absichern

Ziel: Der aktuelle Produktivstand bleibt stabil.

Umsetzung:

- Firmware mit lokalem Fix fuer manuelle Close-Dauer per OTA einspielen.
  - Erledigt am 2026-07-11 09:52 CEST.
- Nach OTA testen:
  - manueller Start V1 fuer kurze Dauer
  - manueller Stop
  - Eventlog-Dauer darf nicht mehr Uptime in Minuten zeigen
  - `ALLE STOP` laesst alle Relais aus
  - Erledigt: Eventlog-Dauer passt.
- Backend-Konfig pruefen:
  - V1-V4: WH52/GW1200 Channel 1-4
  - V5/V6: beide WH52/GW1200 Channel 5
- Ecowitt-Werte pruefen:
  - Channel 1-5 kommen im Backend an
  - History zeigt mehr als nur einen kurzen Zeitraum, sobald genug Daten
    gesammelt wurden
  - Erledigt: 7-Tage-History zeigt echte Daten, Version `1.9.6` online.

Akzeptanz:

- Keine falschen `1145x min`-Events mehr.
- ESP bleibt online.
- Keine Zone schaltet unerwartet.
Status: erledigt am 2026-07-11.

### P1: Sensor-/History-Darstellung mit V5/V6-Shared-Sensor

Mehrwert: Die App zeigt echte Bodenfeuchte sauber und macht transparent, dass
V5/V6 nur einen gemeinsamen Indikator nutzen.

Backend:

- `/api/irrigation/history` ist bereits auf einen langen Verlauf vorbereitet
  (`LIMIT_HISTORY_SENSORS = 60000`). Heute pruefen, ob Produktion diesen Stand
  hat und die DB genug Daten liefert.
- Optional `/api/irrigation/history` um `channels`/`assignments` ergaenzen,
  damit die UI nicht aus Zone 1-6 auf Sensorlinien schliessen muss.
- `/api/irrigation/weather-snapshot` anpassen:
  - nicht nur erste Zone pro Channel als `zoneName` liefern.
  - pro Sensor eine Liste der zugeordneten Zonen liefern, z.B.
    `zones: [{ valveNumber, name }]`.
  - Channel 5 speziell nicht duplizieren, sondern als gemeinsamer Sensor
    ausweisen, wenn mehrere Zonen denselben Channel nutzen.
- Optional Hilfsfunktion:
  - `sensorAssignments(zones)` fuer Channel -> Zonenliste.

Frontend:

- Sensor-Karten/History-Legende:
  - Channel 1-4 als normale Zonen-Sensoren.
  - Channel 5 als `Freiflaeche / Indikator fuer V5+V6`.
  - Hinweis nur dort, wo er hilft; nicht als stoerender Warnblock.
- History-Graph:
  - nur echte Sensor-Channels 1-5 zeichnen.
  - Channels dynamisch aus History-Daten bzw. Sensor-Assignments ableiten,
    nicht aus `IRRIGATION_ZONE_NUMBERS`.
  - nicht Channel 6 als eigene Linie anzeigen, wenn V6 Channel 5 nutzt.
  - Bodenfeuchte-Achse fix 0-100%.
  - Default-Zeitraum kann 24h bleiben, aber 7 Tage muss nach Datensammlung
    sichtbar funktionieren.
- Dashboard:
  - bei V5/V6 Sensorzeile kurz markieren: `Ch 5 geteilt`.

Tests:

- Backend-Test fuer `weather-snapshot` mit zwei Zonen auf demselben Channel.
- Frontend smoke/typecheck.

Akzeptanz:

- V5 und V6 wirken in UI nicht so, als haetten sie zwei separate Sensoren.
- Channel 5 ist sichtbar, aber fachlich richtig gelabelt.
- Bei 7-Tage-History wird kein leerer Channel 6 mehr in Legende oder Graph
  angeboten.
- Wenn die API 7 Tage Daten liefert, kann die UI sie anzeigen.

### P2: Eventlog/History-Auswertung

Status: gestrichen am 2026-07-11. Der aktuelle Eventlog/History-Stand reicht
fuer die Produktivnutzung aus.

Backend:

- `/api/irrigation/events` erweitern:
  - Query-Parameter `days`, `zoneNumber`, `action`, `reason`.
  - Default weiter klein halten, aber Zeitraumfilter erlauben.
- Neuer Summary-Endpunkt oder Dashboard-Zusatz:
  - Laufzeit pro Zone im Zeitraum.
  - Anzahl `open`, `close`, `skip`.
  - Skips nach Grund gruppieren.
- Export:
  - JSON reicht zuerst.
  - CSV spaeter, falls praktisch.

Frontend:

- Eventlog:
  - Zeitraum-Auswahl: 24h, 7 Tage, 30 Tage.
  - Zone-Auswahl: Alle, V1-V6.
  - Gruppierung optional: Tag oder Zone.
  - Summenzeile: Laufzeit, Laeufe, Skips.
- History:
  - Zeitraum-Auswahl bleibt, aber mit klaren Presets.
  - Export-Button fuer sichtbare Daten.

Tests:

- Backend-Tests fuer Filterkombinationen.
- Summary-Test mit gemischten Events.

Akzeptanz:

- 7-Tage-Blick ist nutzbar.
- Skips und Laufzeiten sind ohne manuelles Zaehlen sichtbar.

### P3: Run-Once / Testprogramm

Mehrwert: Wartung und Tests werden deutlich einfacher als sechs einzelne
manuelle Starts.

Backend:

- Neue Command-Art vorbereiten:
  - Variante A: kein neues Command; Web-App legt nacheinander einzelne
    `open`-Commands an. Nur sinnvoll, wenn ESP Queue sauber kann.
  - Variante B: neues `run_once`-Command mit Payload im `raw`/neuem Feld.
  - Empfehlung: erst Runtime-Queue klaeren, dann `run_once` bauen.
- Bis dahin kleiner Zwischenstand:
  - Frontend-Quick-Test kann Zone fuer Zone manuell anstossen, aber nur wenn
    gerade nichts laeuft.

Frontend:

- Neue Ansicht oder Abschnitt unter `Manuell`:
  - Dauer pro Zone.
  - Checkbox/Toggle pro Zone.
  - Button `Testlauf starten`.
  - Quick-Preset: alle aktiven Zonen je 60 Sekunden.
- Queue-Strategie im ersten Schritt:
  - Wenn ein Ventil laeuft: Start ablehnen und klar anzeigen.
  - Kein automatisches Anhaengen, solange ESP nur einen frischen Command pollt.

Firmware-Abhaengigkeit:

- Fuer echtes Run-Once braucht der ESP eine robuste Queue:
  - mehrere Jobs annehmen.
  - Restzeit und queued Status melden.
  - `close_all` leert alles.

Akzeptanz:

- Man kann vor Ort alle Zonen kurz testen, ohne sechs Dialoge zu bedienen.
- Kein Run-Once startet parallel oder unkontrolliert.

### P4: Program Preview

Mehrwert: Vor der Nacht sehen, was der Scheduler voraussichtlich tut.

Backend:

- Neuer Endpunkt:
  - `GET /api/irrigation/preview?days=7`
- Berechnet aus:
  - aktiven Schedules
  - Zone aktiv/inaktiv
  - WH52-Zuordnung
  - aktuellem letztem Sensorwert
  - Feuchte-/Temperatur-Grenzen
- Ergebnis:

```json
{
  "days": 7,
  "items": [
    {
      "date": "2026-07-11",
      "startTime": "02:55",
      "zoneNumber": 2,
      "zoneName": "Hecke/Rasen",
      "durationMin": 55,
      "expected": "run",
      "reason": "Feuchte 24% < 45%"
    }
  ]
}
```

Grenze der Vorschau:

- Sie ist eine Plausibilitaetsvorschau, keine harte Zusage.
- Wetter/Regen und Sensorwerte koennen sich bis zum Lauf aendern.

Frontend:

- Neuer Tab oder Bereich in `Programme`: `Vorschau`.
- Liste nach Tagen gruppieren.
- Farblogik:
  - `run` neutral/gruen
  - `skip` gelb
  - `inactive` grau
- Keine grosse Timeline im ersten Schritt; eine dichte Liste reicht.

Tests:

- Preview mit aktiver/inaktiver Zone.
- Preview mit feuchtem/trockenem Sensorwert.
- Preview mit V5/V6 Shared Channel.

Akzeptanz:

- Nutzer sieht auf einen Blick, welche Zone wann laufen wuerde und warum.

### P5: Runtime-State und Queue

Mehrwert: Dashboard muss Laufzeit/Queue nicht aus Events erraten.

Firmware:

- Runtime-State pro Zone einfuehren:
  - `idle`
  - `running`
  - `queued`
  - `remainingSec`
- Queue eindeutig modellieren:
  - pro Zone maximal ein wartender Job.
  - `close(zone)` entfernt laufenden oder wartenden Job.
  - `close_all` leert alles.
- Status-POST erweitern:

```json
{
  "valveStates": "010000",
  "runtime": {
    "queueLength": 1,
    "zones": [
      {"zone": 1, "state": "idle", "remainingSec": 0},
      {"zone": 2, "state": "running", "remainingSec": 831},
      {"zone": 3, "state": "queued", "remainingSec": 1500}
    ]
  }
}
```

Backend:

- `irrigation_status.raw.runtime` zunaechst ohne DB-Migration nutzen.
- Spaeter, falls noetig, eigene Spalten/Tabelle.

Frontend:

- Dashboard:
  - `running`, `queued`, `idle` anzeigen.
  - Restzeit aus Runtime-State statt Eventlog.
  - Queue-Laenge in Statuskarte.

Akzeptanz:

- Restzeit bleibt nach Refresh korrekt.
- Wartende Laeufe sind sichtbar.

### P6: Globale Sperren und Rain Delay

Mehrwert: Betriebszustand wird sichtbar und steuerbar.

Backend:

- Konfig/Statusmodell fuer:
  - controller enabled/disabled
  - pause until
  - rain delay until
  - sensor ignore pro Zone
- Erst planen, wenn klar ist, ob diese Flags im ESP oder Backend fuehrend
  sein sollen.

Frontend:

- Statusbanner oben im Dashboard:
  - System aktiv/deaktiviert.
  - Pause/Rain Delay mit Endzeit.
  - Direktes Aufheben per Button.
- Zone-Settings:
  - `Sensor ignorieren` nur bewusst und sichtbar.

Akzeptanz:

- Eine Sperre ist sofort sichtbar und nicht nur im Log versteckt.

### P7: Import/Export Bewaesserung

Mehrwert: Vor groesseren Tests kann die Konfiguration gesichert werden.

Backend:

- `GET /api/irrigation/export`
  - zones
  - schedules
  - relevante Grenzwerte
  - keine Events/Sensor-History im ersten Schritt
- `POST /api/irrigation/import`
  - validieren
  - nur Bewaesserungsteil schreiben
  - Valve-Nummern nicht unkontrolliert aendern

Frontend:

- Button in Programme/Settings-Bereich der Bewaesserung.
- Import zeigt vor dem Anwenden eine kurze Zusammenfassung.

Akzeptanz:

- Bewaesserungssetup kann gesichert und wiederhergestellt werden, ohne andere
  App-Bereiche zu beruehren.

## Empfohlene Reihenfolge ab jetzt

1. ESP/TFT-Sensor-History ausbauen, per OTA flashen und pruefen.
2. Danach neuen schlanken Folgeplan erstellen.
3. P4 Program Preview als optionaler Web-App-Mehrwert.
4. P5 Runtime-State/Queue, bevor echtes Run-Once/Queue-Handling kommt.
5. P3 Run-Once/Testprogramm nach P5 vollstaendig bauen.
6. P6 Sperren/Rain Delay und P7 Import/Export nur bei konkretem Bedarf.

## Nicht jetzt

- MQTT-Command-Umbau.
- Master-/Pumpenrelais.
- Wettermodell/Water-Level-Prozent.
- Eventlog/History-Komfortausbau mit mehr Filtern, Summen und Export.
- Komplexe Timeline-Komponente.
