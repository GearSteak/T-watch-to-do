#pragma once

#include <stdint.h>

class DisplaySleep {
public:
    void begin(uint8_t activeBrightness);
    void loopTick(bool blockSleep = false);
    void wake();
    bool isAsleep() const;

private:
    void sleep();

    uint8_t activeBrightness_ = 180;
    bool asleep_ = false;
};

extern DisplaySleep displaySleep;
