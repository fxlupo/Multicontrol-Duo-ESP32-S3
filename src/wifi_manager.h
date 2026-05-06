#pragma once
#include <Arduino.h>

namespace wifi {
    void connect();           // Blockiert bis Verbindung steht (max. 20s)
    void loop();              // Reconnect-Handler, in loop() aufrufen
    bool isConnected();
    bool ntpSynced();
    int8_t rssi();
    uint32_t uptimeSec();     // Sekunden seit Boot
}
