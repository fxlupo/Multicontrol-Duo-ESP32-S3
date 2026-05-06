#pragma once
#include <Arduino.h>

namespace notify {
    void init();
    void loop();

    bool enabled();
    bool hasRecipients();

    // Stellt eine WhatsApp-Nachricht fuer alle aktiven Empfaenger in die Queue.
    bool enqueue(const char* title, const String& message);
}
