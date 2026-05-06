#pragma once
#include <Arduino.h>

// ── Gemeinsame Datenstrukturen (alle Module) ─────────────

struct ZoneConfig {
    uint8_t  id;
    char     name[51];
    uint8_t  wh52_channel;
    uint8_t  moisture_threshold;
    float    temp_minimum;
    float    rain_threshold_6h;
    float    temp_factor_above;
    float    temp_factor_mult;
    uint16_t max_duration_min;
    bool     active;
};

struct Schedule {
    uint16_t id;
    uint8_t  zone_id;
    char     program;       // 'A','B','C'
    uint8_t  start_hour;
    uint8_t  start_min;
    uint16_t duration_min;
    uint8_t  weekdays;      // Bit0=Mo .. Bit6=So
    bool     active;
};

struct ManualCommand {
    char     id[40];
    uint8_t  zone_id;
    char     command[12];   // "open","close","close_all"
    uint16_t duration_min;
};

namespace cfg {
    extern ZoneConfig     zones[4];
    extern uint8_t        zoneCount;
    extern Schedule       schedules[12];
    extern uint8_t        schedCount;
    extern ManualCommand  commands[5];
    extern uint8_t        cmdCount;
    extern uint32_t       version;

    void loadFromNVS();
    bool sync();            // GET /config – true wenn neue Daten
    void ackCommands();     // POST /config/ack für alle pending commands
    bool backendOk();       // letzter erfolgreicher Backend-Kontakt frisch?
    unsigned long lastBackendOkMs();
}
