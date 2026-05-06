#pragma once
#include <Arduino.h>

namespace scheduler {
    void trackManualOpen(uint8_t zone, uint32_t durationMs);
    void clearManualRun(uint8_t zone);
    void clearManualRuns();
    void tick();    // Einmal pro Minute aufrufen: Zeitpläne prüfen, Queue befüllen
    void update();  // Jede loop()-Iteration: laufende Jobs überwachen, nächsten starten
}
