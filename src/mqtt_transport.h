#pragma once
#include <Arduino.h>

namespace mqtt {
    using CommandCallback = void (*)(const char* id, const String& payload);

    void init();
    void loop();
    void setCommandCallback(CommandCallback callback);
    bool publishJson(const char* topicSuffix, const String& body, bool retained = false);
    bool enabled();
    bool connected();
}
