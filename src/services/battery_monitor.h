#pragma once

#include <Arduino.h>
#include <functional>

struct BatteryState {
    int percent = 0;
    bool charging = false;
    uint32_t lastUpdateMs = 0;
};

class BatteryMonitor {
public:
    using AlertCallback = std::function<void(int level, int percent)>;

    void begin();
    void update();
    void resetPrefs();
    void setAlertCallback(AlertCallback cb) { alertCb_ = std::move(cb); }

    const BatteryState &state() const { return state_; }
    int pollIntervalMs() const;

private:
    BatteryState state_;
    AlertCallback alertCb_;
    uint32_t lastPollMs_ = 0;
    bool alerted20_ = false;
    bool alerted10_ = false;
    bool alerted5_ = false;

    void checkThresholds();
    void resetAlertsIfRecovered();
};

extern BatteryMonitor batteryMonitor;
