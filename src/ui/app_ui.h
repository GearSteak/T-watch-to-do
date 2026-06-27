#pragma once

#include <lvgl.h>

namespace AppUi {
    void init();
    void refreshTodos();
    void refreshWatchface();
    void refreshStatusBar();
    void showBatteryAlert(int level, int percent);
    void showAlarmAlert(const char *label, const char *title = "Alarm");
    void showReminderAlert(const char *text);
    void notifyTodoAdded();
    void vibrateShort();
    bool isAlertVisible();
}
