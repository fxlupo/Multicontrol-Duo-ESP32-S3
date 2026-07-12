#pragma once
#include <Arduino.h>

namespace ota {
    void init();
    void startWindow(uint32_t windowMs);
    void stop();
    void handle();
    void markRunningAppValidIfStable();

    bool active();
    bool running();
    uint8_t progress();
    uint32_t secondsLeft();
    const char* statusText();
    const char* hostname();
}
