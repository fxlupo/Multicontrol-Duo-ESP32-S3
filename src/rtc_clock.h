#pragma once

namespace rtc {
    void init();
    bool available();
    bool syncSystemFromRtc();
    bool writeFromSystem();
}
