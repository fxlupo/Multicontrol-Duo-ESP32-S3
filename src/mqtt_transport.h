#pragma once
#include <Arduino.h>

namespace mqtt {
    void init();
    void loop();
    bool publishJson(const char* topicSuffix, const String& body, bool retained = false);
    bool enabled();
    bool connected();
}
