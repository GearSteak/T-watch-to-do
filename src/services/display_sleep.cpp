#include "display_sleep.h"
#include "config.h"

#include <LilyGoLib.h>
#include <lvgl.h>

DisplaySleep displaySleep;

void DisplaySleep::begin(uint8_t activeBrightness) {
    activeBrightness_ = activeBrightness;
    lastActivityMs_ = millis();
    asleep_ = false;
}

void DisplaySleep::onTouch() {
    if (asleep_) {
        wake();
        return;
    }
    lastActivityMs_ = millis();
}

void DisplaySleep::loopTick(bool blockSleep) {
    if (asleep_ || blockSleep) {
        return;
    }
    if (millis() - lastActivityMs_ >= DISPLAY_SLEEP_TIMEOUT_MS) {
        sleep();
    }
}

void DisplaySleep::wake() {
    if (!asleep_) {
        lastActivityMs_ = millis();
        return;
    }
    asleep_ = false;
    lastActivityMs_ = millis();
    instance.setBrightness(activeBrightness_);
    lv_obj_invalidate(lv_scr_act());
}

bool DisplaySleep::isAsleep() const {
    return asleep_;
}

void DisplaySleep::sleep() {
    asleep_ = true;
    // setBrightness(0) turns off the backlight and sends the panel sleep command.
    instance.setBrightness(0);
}
