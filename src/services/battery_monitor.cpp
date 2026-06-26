#include "battery_monitor.h"
#include "config.h"

#include <LilyGoLib.h>
#include <Preferences.h>

BatteryMonitor batteryMonitor;

static Preferences batteryPrefs;

void BatteryMonitor::begin() {
    batteryPrefs.begin("battery", false);
    alerted20_ = batteryPrefs.getBool("a20", false);
    alerted10_ = batteryPrefs.getBool("a10", false);
    alerted5_ = batteryPrefs.getBool("a5", false);

    instance.pmu.enableBattDetection();
    instance.pmu.enableBattVoltageMeasure();
    instance.pmu.enableVbusVoltageMeasure();

    update();
}

int BatteryMonitor::pollIntervalMs() const {
    return state_.percent <= BATTERY_LOW_THRESHOLD ? BATTERY_POLL_MS_LOW : BATTERY_POLL_MS_NORMAL;
}

void BatteryMonitor::resetAlertsIfRecovered() {
    if (state_.percent > BATTERY_LOW_THRESHOLD || state_.charging) {
        if (alerted20_ || alerted10_ || alerted5_) {
            alerted20_ = alerted10_ = alerted5_ = false;
            batteryPrefs.putBool("a20", false);
            batteryPrefs.putBool("a10", false);
            batteryPrefs.putBool("a5", false);
        }
    }
}

void BatteryMonitor::checkThresholds() {
    if (state_.charging) {
        return;
    }

    auto fire = [&](int level, bool &flag) {
        if (state_.percent <= level && !flag) {
            flag = true;
            batteryPrefs.putBool(level == 20 ? "a20" : level == 10 ? "a10" : "a5", true);
            if (alertCb_) {
                alertCb_(level, state_.percent);
            }
        }
    };

    fire(20, alerted20_);
    fire(10, alerted10_);
    fire(5, alerted5_);
}

void BatteryMonitor::resetPrefs() {
    alerted20_ = alerted10_ = alerted5_ = false;
    batteryPrefs.clear();
}

void BatteryMonitor::update() {
    const uint32_t now = millis();
    if (now - lastPollMs_ < (uint32_t)pollIntervalMs()) {
        return;
    }
    lastPollMs_ = now;

    state_.percent = instance.pmu.getBatteryPercent();
    state_.charging = instance.pmu.isCharging();
    state_.lastUpdateMs = now;

    resetAlertsIfRecovered();
    checkThresholds();
}
