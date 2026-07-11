#pragma once
#include <Arduino.h>

namespace valve {
    struct Settings {
        uint16_t openPulseMs;
        uint16_t closePulseMs;
        uint8_t  closeDuty;
        uint16_t seqPauseMs;
    };

    void init();                          // GPIOs einrichten, alle Ventile schließen
    void closeAll();                      // Failsafe: alle schließen (auch ohne init)

    bool open(uint8_t zone, uint32_t maxDurationMs);
    void close(uint8_t zone);
    void update();                        // Max-Timer prüfen, in loop() aufrufen
    void lockOpens(uint32_t durationMs);  // Failsafe: Oeffnen temporaer sperren

    bool     isOpen(uint8_t zone);
    uint8_t  getOpenZone();               // 0 wenn keines offen
    String   stateStr();                  // z.B. "000000" .. "100000"

    Settings settings();
    void setOpenPulseMs(uint16_t ms);
    void setClosePulseMs(uint16_t ms);
    void setCloseDuty(uint8_t duty);
    void setSeqPauseMs(uint16_t ms);
    void resetSettings();
}
