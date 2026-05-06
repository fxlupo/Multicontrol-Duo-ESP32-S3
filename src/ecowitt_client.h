#pragma once
#include <Arduino.h>

#define ECOWITT_MAX_SOIL_CHANNELS 16

namespace ecowitt {

    struct SoilData {
        uint8_t       channel;
        float         moisture;      // % (NAN wenn nicht verfügbar)
        float         temp;          // °C
        float         ec;            // µS/cm
        bool          valid;
        unsigned long ts;            // millis() Zeitstempel
    };

    struct WeatherData {
        float rain_rate;             // mm/h (NAN wenn kein Sensor)
        float rain_6h;               // mm   (NAN wenn kein Sensor)
        bool  hasRain;
    };

    extern SoilData    soil[ECOWITT_MAX_SOIL_CHANNELS]; // Index = channel-1
    extern WeatherData weather;

    bool poll();                     // HTTP GET GW1200, true bei Erfolg
    bool isFresh(uint8_t channel);   // Daten < 15 min alt?
    bool ecowittOk();                // Letzter Poll < 30 min?
}
