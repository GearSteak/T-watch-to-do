#include "display_sleep.h"
#include "config.h"

#include <LilyGoLib.h>
#include <lvgl.h>

DisplaySleep displaySleep;

void DisplaySleep::begin(uint8_t activeBrightness) {
    activeBrightness_ = activeBrightness;
    asleep_ = false;
}

void DisplaySleep::loopTick(bool blockSleep) {
    // Use LVGL's own input-inactivity timer instead of polling the touch
    // controller ourselves. Reading the touch panel from two places (here and
    // LVGL's input driver) contends on the shared I2C bus and could wedge the
    // UI, which looked like random freezes.
    const uint32_t idleMs = lv_display_get_inactive_time(NULL);

    if (asleep_) {
        // Any touch resets LVGL's inactivity timer, so a low value means the
        // user just interacted and we should turn the screen back on.
        if (idleMs < DISPLAY_SLEEP_TIMEOUT_MS) {
            wake();
        }
        return;
    }

    if (blockSleep) {
        return;
    }
    if (idleMs >= DISPLAY_SLEEP_TIMEOUT_MS) {
        sleep();
    }
}

void DisplaySleep::wake() {
    if (!asleep_) {
        return;
    }
    asleep_ = false;
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
