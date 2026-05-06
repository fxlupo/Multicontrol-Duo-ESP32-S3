#include "scheduler.h"
#include "config.h"
#include "config_sync.h"
#include "valve_driver.h"
#include "ecowitt_client.h"
#include "event_logger.h"
#include <time.h>

struct WaterJob {
    uint8_t  zone;
    uint32_t durationSec;
    bool     started;
    char     reason[12];
    char     detail[64];
    float    moisture;
    float    temp;
    float    ec;
    float    rain_6h;
};

static constexpr uint8_t MAX_WATER_QUEUE = 12;
static WaterJob  _queue[MAX_WATER_QUEUE];
static int8_t    _qLen         = 0;
static int8_t    _currentJob   = -1;
static unsigned long _jobPauseUntil = 0;

struct ManualRun {
    bool active;
    unsigned long startedAt;
    uint32_t durationMs;
};
static ManualRun _manualRuns[4] = {};

// Letzter Trigger pro Schedule (Schedule-ID → Minute-des-Tages + Wochentag)
struct LastTrig { uint16_t id; int minOfDay; int wday; };
static LastTrig _lastTrig[12] = {};

static bool alreadyTriggered(uint16_t id, int minOfDay, int wday) {
    for (uint8_t i = 0; i < 12; i++) {
        if (_lastTrig[i].id == id && _lastTrig[i].minOfDay == minOfDay &&
            _lastTrig[i].wday == wday) return true;
    }
    return false;
}
static void markTriggered(uint16_t id, int minOfDay, int wday) {
    for (uint8_t i = 0; i < 12; i++) {
        if (_lastTrig[i].id == id || _lastTrig[i].id == 0) {
            _lastTrig[i] = {id, minOfDay, wday};
            return;
        }
    }
}

// Bit0=Mo..Bit6=So; tm_wday: 0=So,1=Mo..6=Sa
static bool weekdayMatch(uint8_t mask, int tm_wday) {
    int bit = (tm_wday == 0) ? 6 : tm_wday - 1;
    return (mask >> bit) & 1;
}

#ifndef SCHEDULER_MISSING_SENSOR_MODE
#define SCHEDULER_MISSING_SENSOR_MODE 1
#endif

#ifndef SCHEDULER_SENSOR_FALLBACK_PERCENT
#define SCHEDULER_SENSOR_FALLBACK_PERCENT 50
#endif

#ifndef SCHEDULER_IGNORE_SENSOR_CHECKS
#define SCHEDULER_IGNORE_SENSOR_CHECKS 0
#endif

static uint8_t fallbackPercent() {
    return min<uint8_t>(100, max<uint8_t>(1, SCHEDULER_SENSOR_FALLBACK_PERCENT));
}

static WaterJob* enqueueJob(uint8_t zone, uint32_t durationSec) {
    if (_qLen >= MAX_WATER_QUEUE) return nullptr;
    WaterJob& job = _queue[_qLen++];
    memset(&job, 0, sizeof(job));
    job.zone        = zone;
    job.durationSec = durationSec;
    job.started     = false;
    job.moisture = job.temp = job.ec = job.rain_6h = NAN;
    return &job;
}

void scheduler::trackManualOpen(uint8_t zone, uint32_t durationMs) {
    if (zone < 1 || zone > 4 || durationMs == 0) return;
    _manualRuns[zone - 1] = {true, millis(), durationMs};
}

void scheduler::clearManualRun(uint8_t zone) {
    if (zone < 1 || zone > 4) return;
    _manualRuns[zone - 1].active = false;
}

void scheduler::clearManualRuns() {
    for (uint8_t i = 0; i < 4; i++) {
        _manualRuns[i].active = false;
    }
}

static void checkManualRunCompletions() {
    for (uint8_t i = 0; i < 4; i++) {
        ManualRun& run = _manualRuns[i];
        if (!run.active) continue;
        uint8_t zone = i + 1;
        if (valve::isOpen(zone)) continue;

        uint32_t elapsedMs = millis() - run.startedAt;
        bool expired = elapsedMs + 1000UL >= run.durationMs;
        run.active = false;

        if (expired) {
            uint32_t elapsedSec = max<uint32_t>(1, elapsedMs / 1000UL);
            events::log(zone, "close", "manual", "Dauer abgelaufen",
                elapsedSec, NAN, NAN, NAN, NAN);
            events::flush();
        }
    }
}

static void handleManualCommands() {
    for (uint8_t i = 0; i < cfg::cmdCount; i++) {
        ManualCommand& mc = cfg::commands[i];
        if (strcmp(mc.command, "close_all") == 0) {
            valve::closeAll();
            scheduler::clearManualRuns();
            events::log(0, "close", "manual", "close_all", 0, NAN, NAN, NAN, NAN);
        } else if (strcmp(mc.command, "open") == 0) {
            uint32_t durMs = (uint32_t)mc.duration_min * 60000UL;
            bool wasOpen = valve::isOpen(mc.zone_id);
            if (valve::open(mc.zone_id, durMs)) {
                char det[64];
                snprintf(det, sizeof(det), "Manuell %u min", mc.duration_min);
                events::log(mc.zone_id, "open", "manual", det, 0, NAN, NAN, NAN, NAN);
                if (!wasOpen) scheduler::trackManualOpen(mc.zone_id, durMs);
            }
        } else if (strcmp(mc.command, "close") == 0) {
            uint32_t durSec = valve::isOpen(mc.zone_id)
                ? (uint32_t)((millis() - 0) / 1000) : 0;  // Laufzeit nicht direkt verfügbar
            valve::close(mc.zone_id);
            scheduler::clearManualRun(mc.zone_id);
            events::log(mc.zone_id, "close", "manual", "close", durSec, NAN, NAN, NAN, NAN);
        }
    }
    if (cfg::cmdCount > 0) cfg::ackCommands();
}

void scheduler::tick() {
    // Manuelle Befehle haben höchste Priorität
    if (cfg::cmdCount > 0) {
        handleManualCommands();
        return;
    }

    struct tm t;
    if (!getLocalTime(&t, 100)) return;
    int minOfDay = t.tm_hour * 60 + t.tm_min;

    if (_currentJob < 0) _qLen = 0;

    for (uint8_t zi = 0; zi < cfg::zoneCount; zi++) {
        const ZoneConfig& z = cfg::zones[zi];
        if (!z.active) continue;

        for (uint8_t si = 0; si < cfg::schedCount; si++) {
            const Schedule& s = cfg::schedules[si];
            if (!s.active || s.zone_id != z.id) continue;
            if (s.start_hour != t.tm_hour || s.start_min != t.tm_min) continue;
            if (!weekdayMatch(s.weekdays, t.tm_wday)) continue;
            if (alreadyTriggered(s.id, minOfDay, t.tm_wday)) continue;

            markTriggered(s.id, minOfDay, t.tm_wday);

            // ── Entscheidungsbaum ──────────────────────
            bool hasFreshSensor = ecowitt::isFresh(z.wh52_channel);
            const ecowitt::SoilData& sd = hasFreshSensor
                ? ecowitt::soil[z.wh52_channel - 1]
                : ecowitt::soil[0];
            float durationSec = s.duration_min * 60.0f;

#if SCHEDULER_IGNORE_SENSOR_CHECKS
            durationSec = min<float>(durationSec, (float)z.max_duration_min * 60.0f);
            WaterJob* job = enqueueJob(z.id, (uint32_t)durationSec);
            if (job) {
                strlcpy(job->reason, "schedule", sizeof(job->reason));
                snprintf(job->detail, sizeof(job->detail), "Sensorcheck aus, Prog %c", s.program);
            } else {
                events::log(z.id, "skip", "system", "Queue voll", 0, NAN, NAN, NAN, NAN);
            }
            break;
#else
            if (!hasFreshSensor) {
#if SCHEDULER_MISSING_SENSOR_MODE == 0
                events::log(z.id, "skip", "sensor", "Keine Sensor Daten", 0,
                    NAN, NAN, NAN, NAN);
                continue;
#else
                // Fallback-Modus: kein Sensorcheck, aber definierte Laufzeit.
                uint8_t pct = fallbackPercent();
                durationSec *= ((float)pct / 100.0f);
                WaterJob* job = enqueueJob(z.id, (uint32_t)durationSec);
                if (job) {
                    strlcpy(job->reason, "schedule",               sizeof(job->reason));
                    snprintf(job->detail, sizeof(job->detail), "Sensor-Fallback %u%%", pct);
                } else {
                    events::log(z.id, "skip", "system", "Queue voll", 0, NAN, NAN, NAN, NAN);
                }
                continue;
#endif
            }

            // Bodentemperatur prüfen
            if (!isnan(sd.temp) && sd.temp < z.temp_minimum) {
                char det[64];
                snprintf(det, sizeof(det), "Boden zu kalt: %.1f°C", sd.temp);
                events::log(z.id, "skip", "sensor", det, 0,
                    sd.moisture, sd.temp, sd.ec, NAN);
                continue;
            }

            // Bodenfeuchte prüfen
            if (!isnan(sd.moisture) && sd.moisture >= z.moisture_threshold) {
                char det[64];
                snprintf(det, sizeof(det), "Boden feucht: %.0f%%", sd.moisture);
                events::log(z.id, "skip", "sensor", det, 0,
                    sd.moisture, sd.temp, sd.ec, NAN);
                continue;
            }

            // Regencheck (nur wenn Sensor vorhanden)
            if (ecowitt::weather.hasRain && !isnan(ecowitt::weather.rain_6h)) {
                if (ecowitt::weather.rain_6h >= z.rain_threshold_6h) {
                    char det[64];
                    snprintf(det, sizeof(det), "Regen: %.1f mm/6h", ecowitt::weather.rain_6h);
                    events::log(z.id, "skip", "sensor", det, 0,
                        sd.moisture, sd.temp, sd.ec, ecowitt::weather.rain_6h);
                    continue;
                }
            }

            // Dauer berechnen
            if (!isnan(sd.temp) && sd.temp > z.temp_factor_above) {
                durationSec *= z.temp_factor_mult;
            }
            uint32_t maxSec = (uint32_t)z.max_duration_min * 60;
            if (durationSec > maxSec) durationSec = maxSec;

            WaterJob* job = enqueueJob(z.id, (uint32_t)durationSec);
            if (job) {
                strlcpy(job->reason, "schedule", sizeof(job->reason));
                snprintf(job->detail, sizeof(job->detail), "Prog %c, Feuchte %.0f%%",
                    s.program, isnan(sd.moisture) ? 0.0f : sd.moisture);
                job->moisture = sd.moisture;
                job->temp     = sd.temp;
                job->ec       = sd.ec;
                job->rain_6h  = ecowitt::weather.hasRain ? ecowitt::weather.rain_6h : NAN;
            } else {
                events::log(z.id, "skip", "system", "Queue voll", 0,
                    sd.moisture, sd.temp, sd.ec,
                    ecowitt::weather.hasRain ? ecowitt::weather.rain_6h : NAN);
            }
            break;  // pro Zone nur ein Schedule pro Minute
#endif
        }
    }

    if (_qLen > 0 && _currentJob < 0) {
        _currentJob = 0;
        _jobPauseUntil = 0;
    }
}

void scheduler::update() {
    checkManualRunCompletions();

    if (_currentJob < 0 || _currentJob >= _qLen) return;
    if (millis() < _jobPauseUntil) return;

    WaterJob& job = _queue[_currentJob];

    if (!valve::isOpen(job.zone)) {
        // Entweder noch nicht geöffnet oder gerade geschlossen (Max-Timer)
        if (!job.started) {
            if (valve::getOpenZone() > 0) return;
            // Job starten
            if (valve::open(job.zone, job.durationSec * 1000UL)) {
                events::log(job.zone, "open", job.reason, job.detail,
                    job.durationSec, job.moisture, job.temp, job.ec, job.rain_6h);
                job.started = true;
            }
        } else {
            // Job beendet (Max-Timer hat geschlossen oder manuell)
            events::log(job.zone, "close", job.reason, "Max-Timer", 0,
                NAN, NAN, NAN, NAN);
            events::flush();
            _currentJob++;
            if (_currentJob >= _qLen) {
                _qLen = 0; _currentJob = -1;
            } else {
                _jobPauseUntil = millis() + VALVE_SEQ_PAUSE_MS;
            }
        }
    }
}
