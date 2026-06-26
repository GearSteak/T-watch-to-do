#pragma once

#include <Arduino.h>
#include <functional>

class AlarmService {
public:
    void begin();
    void loopTick();

    void setOnFire(std::function<void(const String &label)> cb) { onFire_ = std::move(cb); }

private:
    std::function<void(const String &)> onFire_;
    uint16_t lastCheckedHm_ = 0xFFFF;
};

extern AlarmService alarmService;
