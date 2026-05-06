#pragma once
#include <Arduino.h>

namespace events {
    struct RecentEvent {
        uint8_t zone_id;
        char action[8];
        char reason[12];
        char detail[64];
        char created_at[20];
    };

    void log(uint8_t zone_id, const char* action, const char* reason,
             const char* detail, uint32_t duration_sec,
             float moisture, float temp, float ec, float rain_6h);

    void flush();           // Gepufferte Events an Backend senden

    void postStatus();      // POST /status Heartbeat

    void uploadSensors();   // POST /sensors mit aktuellen Ecowitt-Daten

    bool loadRecentFromBackend(uint8_t limit = 12);
    uint8_t recentCount();
    bool recentAt(uint8_t newestIndex, RecentEvent& out);
}
