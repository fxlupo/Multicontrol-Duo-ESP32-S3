#pragma once

namespace display {
    void init();
    void refresh();       // Aktuellen Screen neu zeichnen
    void periodicRefresh(); // Ruhiger Auto-Refresh für Dashboard/Backlight
    void handleTouch();   // Touch-Events verarbeiten (alle 50ms)
}
