#pragma once
#include <Arduino.h>

namespace scheduler {
    enum class RuntimeState : uint8_t {
        Idle,
        Running,
        Queued,
    };

    void trackManualOpen(uint8_t zone, uint32_t durationMs);
    void clearManualRun(uint8_t zone);
    void clearManualRuns();
    void clearQueuedZone(uint8_t zone);
    void clearQueue();
    RuntimeState runtimeState(uint8_t zone);
    uint32_t remainingSec(uint8_t zone);
    uint8_t queueLength();
    void tick();    // Einmal pro Minute aufrufen: Zeitpläne prüfen, Queue befüllen
    void update();  // Jede loop()-Iteration: laufende Jobs überwachen, nächsten starten
}
