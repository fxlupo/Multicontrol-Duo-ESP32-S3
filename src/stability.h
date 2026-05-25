#pragma once
#include <Arduino.h>
#include <esp_system.h>

namespace stability {
    void init();
    void update();
    void mark(const char* stage);

    esp_reset_reason_t resetReason();
    const char* resetReasonText();
    const char* resetReasonCode();
    bool resetWasCrash();

    uint32_t bootCount();
    uint32_t crashCount();
    uint32_t previousUptimeSec();
    uint32_t minFreeHeap();
    const char* bootEventDetail();
    const char* lastBreadcrumbStage();
    uint32_t lastBreadcrumbUptimeSec();
    uint32_t lastBreadcrumbHeap();
}
