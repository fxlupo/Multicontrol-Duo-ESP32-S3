# OpenSprinkler Referenznotizen

Stand: 2026-06-17

Diese Notizen sind nur als externe Inspiration gedacht. Sie sind kein
Umsetzungsplan und ersetzen nicht den Hunter-PVG-/Relais-/MQTT-Umbauplan.

Quelle:

- Repository: https://github.com/OpenSprinkler/OpenSprinkler-Firmware
- Dokumentation/API: https://opensprinkler.github.io/OpenSprinkler-Firmware/
- App: https://github.com/OpenSprinkler/OpenSprinkler-App

Wichtig: OpenSprinkler-Firmware steht unter GPL-3.0. Daher keine Codeuebernahme
in unser Projekt ohne bewusste Lizenzentscheidung. Die folgenden Punkte sind
Architekturideen, keine Kopiervorlage.

Die OpenSprinkler-App steht unter AGPL-3.0. Auch hier gilt: keine
Codeuebernahme; nur Produkt-/UX-Ideen uebernehmen.

Update 2026-07-10:

- Repo/Doku erneut geprueft. Fuer unser Projekt bleiben Queue-/Status- und
  Betriebsflags wichtiger als MQTT.
- MQTT wird bewusst nach unten priorisiert; OpenSprinkler dient vorher als
  Referenz fuer robuste Runtime-Semantik.
- OpenSprinkler-App zusaetzlich geprueft. Fuer uns interessant sind vor allem
  Run-Once, Program Preview, Logs/Timeline, Rain Delay, Import/Export und die
  Analog-Sensor-UI.

## Nuetzliche Konzepte

### Runtime-Queue mit Stationsindex

OpenSprinkler modelliert laufende und wartende Bewaesserungen ueber eine
Runtime-Queue. Zusaetzlich gibt es pro Station einen Index in diese Queue.

Fuer unser Projekt interessant:

- `WaterJob`-Queue beibehalten, aber um `zone_qid[zone]` ergaenzen.
- Doppelte Queue-Eintraege pro Zone vermeiden.
- `close(zone)` kann laufende oder wartende Jobs dieser Zone gezielt entfernen.
- `close_all` kann Queue und laufenden Job eindeutig leeren.
- Restzeit/Queued-Zustand sollte aus Runtime-State kommen, nicht aus Eventlog.

### Queue-Optionen fuer manuelle Starts

OpenSprinkler kennt Queue-Optionen fuer manuelle Laeufe:

- bestehende Queue ersetzen
- an Queue anhaengen
- vorne einfuegen

Fuer uns spaeter denkbar:

- `queueMode: "replace"`
- `queueMode: "append"`
- `queueMode: "front"`
- `queueMode: "reject_if_busy"`

Konservativer Default fuer den ersten Relaisstand:

- `reject_if_busy` oder `replace`, nicht automatisch parallelisieren.

### Statusmodell: running / queued / idle

OpenSprinkler trennt Station-Status und Program-/Queue-Status. Eine Station
kann aus sein und trotzdem einen wartenden Queue-Eintrag haben.

Fuer unser MQTT-Statusmodell sinnvoll:

```json
{
  "zones": [
    {"zone": 1, "state": "running", "remainingSec": 812},
    {"zone": 2, "state": "queued", "remainingSec": 600},
    {"zone": 3, "state": "idle", "remainingSec": 0}
  ],
  "queueLength": 2
}
```

Das waere robuster als Restzeiten im Frontend aus Events zu rekonstruieren.

In der OpenSprinkler-API ist dasselbe Konzept ueber Program-Status und
Queue-Laenge sichtbar: pro Station werden Programm-ID, Restzeit, Startzeit und
sequential group gemeldet; wenn eine Station aus ist, aber eine Programm-ID hat,
ist sie queued. Fuer uns daraus ableiten:

- ESP-Status soll selbst `running`/`queued`/`idle` liefern.
- Dashboard soll nicht mehr raten, ob ein Eintrag wartet.
- Eventlog bleibt Historie, nicht Runtime-State.

### App-Ideen: Run-Once und Testlaeufe

Die OpenSprinkler-App hat eine Run-Once-Seite: fuer jede Station wird eine
einmalige Dauer gesetzt, `0` bedeutet "nicht mitlaufen". Zusaetzlich gibt es
Quick-Picks wie "Test All Stations" und eine Queue-Option, falls bereits etwas
laeuft.

Fuer unser Web-Frontend interessant:

- "Einmalprogramm" fuer Test/Ad-hoc-Bewaesserung.
- Dauer pro Zone in einer kompakten Maske statt sechs einzelner manueller
  Starts.
- Quick-Aktion "alle Zonen je X Sekunden testen".
- Wenn bereits etwas laeuft: bewusst waehlen zwischen abbrechen, anhaengen
  oder ablehnen. Fuer den ersten Schritt reicht `reject_if_busy` oder
  `replace`.

### App-Ideen: Program Preview

Die App simuliert Programme fuer ein gewaehltes Datum und zeigt sie als
Timeline. Dabei werden Queue, sequentielle Gruppen, Wetter-/Water-Level-
Anpassung und deaktivierte Stationen beruecksichtigt.

Fuer uns:

- Vorschau gehoert eher in die Web-App als auf das ESP-TFT.
- Sinnvoller erster Schritt: "naechste 7 Tage" mit geplanten Starts,
  erwarteter Dauer und Sensor-Skip-Hinweis.
- Kein Muss fuer die Relaisstabilisierung, aber sehr hilfreich, um
  Scheduler-Fehler vor der Nacht zu sehen.

### App-Ideen: Logs und Historie

Die App bietet Logs als Timeline und Tabelle, mit Start-/Enddatum, Gruppierung
nach Tag oder Station und Export. Sie zeigt Summen wie Anzahl der Laeufe und
Gesamtlaufzeit.

Fuer uns:

- Eventlog in der App nach Tag/Zone filterbar machen.
- Summen pro Zeitraum: Laufzeit pro Zone, Anzahl Laeufe, Skips.
- Export fuer Diagnose beibehalten.
- Lange Sensor-History gehoert ins Backend; ESP/TFT-History kann entfallen
  oder sehr klein bleiben.

### App-Ideen: Analog-Sensoren

Die Analog-Erweiterung der App trennt Sensor-Konfiguration, Anzeige auf der
Hauptseite und Logging. Sensoren haben u.a. Name, Einheit, Gruppe, Enable,
Log und Show. Fuer kombinierte Sensoren verweist die UI auf Sensorgruppen.

Fuer unsere Ecowitt/GW1200-Anbindung:

- Sensor Channel 1-5 als eigene Sensoren mit Namen/Einheit anzeigen.
- Channel 5 nicht doppelt als zwei echte Zonen-Sensoren darstellen, sondern
  klar als "Freiflaeche / Indikator fuer V5+V6" labeln.
- V5 und V6 duerfen dieselbe Sensorquelle nutzen, aber UI und History muessen
  diese Unschaerfe sichtbar machen.
- History-Graphen nach Sensortyp/Einheit gruppieren; Bodenfeuchte 0-100% mit
  fixem Bereich.

### App-Ideen: Statusleiste und Sperren

Die App priorisiert Statusmeldungen in etwa so:

- System deaktiviert.
- Queue pausiert mit Restzeit.
- laufende Station(en) mit Restzeit.
- Rain Delay bis Zeitpunkt.
- Sensor/Rain aktiv.
- Manual Mode aktiv.

Fuer uns:

- Dashboard braucht einen klaren Anlagenzustand oberhalb der Zonenliste.
- Globale Sperren duerfen nicht nur im Eventlog auftauchen.
- Rain Delay/Pause sollte mit Endzeit bzw. Restzeit sichtbar und direkt
  loeschbar sein.

### App-Ideen: Import/Export

Die App exportiert/importiert Controller-Konfigurationen als JSON und warnt bei
Netzwerkaenderungen.

Fuer uns:

- Bewaesserungs-Konfiguration im Backend exportieren/importieren:
  Zonen, Sensorzuordnung, Programme, Schwellwerte.
- Beim Import nur Bewaesserungsteil anfassen.
- Import vor dem Anwenden validieren und Aenderungen anzeigen.

### MQTT-Betriebsregeln

OpenSprinkler nutzt MQTT als optionale Remote-Control-Schicht. Nuetzliche
Patterns:

- Client-ID aus MAC-Adresse.
- Availability-Topic mit retained `online`/`offline`.
- Last Will auf `offline`.
- Keepalive.
- Reconnect nicht permanent, sondern mit Mindestabstand.
- Publish- und Subscribe-Topic konfigurierbar.

Fuer uns uebernehmen als Idee:

- MQTT-Reconnect nicht blockierend und nicht zu aggressiv.
- LWT/Availability retained.
- Status sofort bei Aenderung und zusaetzlich als Heartbeat.
- Commands in dieselbe Runtime-Queue fuehren wie lokale Scheduler-/Touch-
  Aktionen.

### Master-Valve / Pumpenrelais

OpenSprinkler unterstuetzt Master-Stationen mit Einschalt-/Ausschaltversatz.

Fuer unser 8-Relaisboard als spaetere Option:

- IN1-IN4: Zonen.
- IN5 optional Master-Valve oder Pumpenrelais.
- Master vor Zone einschalten, nach Zone verzoegert ausschalten.
- Master-Logik erst nach stabilem 4-Zonen-Relaisbetrieb planen.

Nach aktuellem 6-Zonen-Stand bleibt das eine spaetere Option fuer IN7/IN8
oder ein dediziertes Pumpenrelais. Nicht in die naechste Produktivrunde ziehen.

### Betriebsflags statt Compile-Flags

OpenSprinkler hat globale und zonenspezifische Betriebsoptionen wie:

- Controller enabled/disabled.
- Rain delay.
- Pause.
- Sensor-/Rain-Ignore pro Zone.
- Water-Level-Prozent.

Fuer uns spaeter interessant:

- Sensorchecks nicht dauerhaft ueber Compile-Flag steuern.
- Backend-Konfiguration um klare Betriebsflags erweitern.
- Dashboard soll anzeigen, ob eine Sperre aktiv ist und ob eine Zone sie
  ignoriert.

Konkret fuer uns:

- `SCHEDULER_IGNORE_SENSOR_CHECKS` langfristig durch Backend-Betriebsflags
  ersetzen.
- Zone 5/6 koennen Channel 5 teilen, sollten aber im UI als weniger praezise
  markiert bleiben.
- Rain delay / Pause als globaler Sperrzustand mit Ablaufzeit modellieren.
- Sensor-/Rain-Ignore pro Zone erst einfuehren, wenn Statusanzeige und Queue
  sauber sind.

### Program Preview / Laufvorschau

OpenSprinkler bietet eine Program Preview, die geplante Laeufe visualisiert.

Fuer uns interessant, aber nicht sofort:

- Im Backend/App eine Vorschau "naechste 7 Tage" erzeugen.
- Pro Zone anzeigen, wann ein Plan laufen wuerde und ob aktuelle Sensorwerte
  voraussichtlich skippen.
- Nicht auf dem ESP/TFT bauen; dort nur Live-Status und manuelle Bedienung.

## Nicht uebernehmen

- Kein direkter Code wegen GPL-3.0.
- Keine Kurzparameter-HTTP-API nachbauen.
- Keine grosse Webserver-/HTML-Integration im ESP.
- Keine Multi-Plattform-Abstraktionen fuer Pi/Linux/AVR.
- Keine Komplexitaet fuer 40 Programme/Expansion-Boards, bevor 4 Relaiszonen
  stabil laufen.

## Relevanz fuer unseren Umbau

Beim Hunter-PVG-/Relais-Umbau zuerst simpel bleiben:

1. Relais-Ausgaenge sicher und idempotent.
2. Eine Zone gleichzeitig.
3. Klare Runtime-Queue.
4. MQTT-Status mit `running`/`queued`/`idle`.
5. Commands ueber MQTT in dieselbe Queue fuehren.

OpenSprinkler ist vor allem eine gute Referenz fuer Queue- und Statussemantik,
nicht fuer direkte Implementierung.
