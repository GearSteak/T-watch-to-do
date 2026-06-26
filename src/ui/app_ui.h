#pragma once

#include <lvgl.h>

namespace AppUi {
    void init();
    void refreshTodos();
    void refreshWatchface();
    void refreshStatusBar();
    void showBatteryAlert(int level, int percent);
    void vibrateShort();
    bool isAlertVisible();
}
