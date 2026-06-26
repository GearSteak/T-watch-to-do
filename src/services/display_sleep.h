#pragma once

#include <stdint.h>

class DisplaySleep {
public:
    void begin(uint8_t activeBrightness);
    void loopTick(bool blockSleep = false);
    void onTouch();
    void wake();
    bool isAsleep() const;

private:
    void sleep();

    uint8_t activeBrightness_ = 180;
    uint32_t lastActivityMs_ = 0;
    bool asleep_ = false;
};

extern DisplaySleep displaySleep;
