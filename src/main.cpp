#include <LilyGoLib.h>
#include <LV_Helper.h>

#include "config.h"
#include "ble/ble_service.h"
#include "storage/todo_store.h"
#include "storage/watchface_store.h"
#include "services/battery_monitor.h"
#include "services/time_service.h"
#include "services/display_sleep.h"
#include "services/alarm_service.h"
#include "storage/alarm_store.h"
#include "ui/app_ui.h"

static uint32_t lastBatteryNotifyMs = 0;
static volatile bool todosDirty = false;
static volatile bool watchfaceDirty = false;
static volatile bool alarmsDirty = false;

static void onTodoChanged() {
    todosDirty = true;
}

static void onWatchfaceChanged() {
    watchfaceDirty = true;
}

static void onAlarmChanged() {
    alarmsDirty = true;
}

static void onBatteryAlert(int level, int percent) {
    AppUi::showBatteryAlert(level, percent);
    bleService.notifyBatteryAlert(level, percent);
    bleService.notifyDeviceInfo();
}

static void onAlarmFire(const String &label) {
    AppUi::showAlarmAlert(label.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[Main] T-Watch Companion starting");

    instance.begin();
    beginLvglHelper(instance);
    instance.setBrightness(DISPLAY_ACTIVE_BRIGHTNESS);
    displaySleep.begin(DISPLAY_ACTIVE_BRIGHTNESS);

    if (!todoStore.begin()) {
        Serial.println("[Main] Todo store init failed");
    }
    todoStore.setOnChange(onTodoChanged);

    if (!watchfaceStore.begin()) {
        Serial.println("[Main] Watchface store init failed");
    }
    watchfaceStore.setOnChange(onWatchfaceChanged);

    if (!alarmStore.begin()) {
        Serial.println("[Main] Alarm store init failed");
    }
    alarmStore.setOnChange(onAlarmChanged);

    batteryMonitor.begin();
    batteryMonitor.setAlertCallback(onBatteryAlert);

    bleService.setTodoChangeCallback(onTodoChanged);
    bleService.setWatchfaceChangeCallback(onWatchfaceChanged);
    bleService.setAlarmChangeCallback(onAlarmChanged);
    bleService.begin();

    alarmService.begin();
    alarmService.setOnFire(onAlarmFire);

    AppUi::init();

    struct tm hwTime;
    instance.rtc.getDateTime(&hwTime);
    struct timeval tv;
    tv.tv_sec = mktime(&hwTime);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    Serial.println("[Main] Ready");
}

void loop() {
    instance.loop();
    batteryMonitor.update();

    bleService.loopTick();

    if (bleService.consumeTodoAddedNotify()) {
        AppUi::notifyTodoAdded();
    }

    displaySleep.loopTick(AppUi::isAlertVisible());

    todoStore.processRepeats();
    alarmService.loopTick();

    String reminderText;
    if (todoStore.dueReminder(reminderText)) {
        AppUi::showReminderAlert(reminderText.c_str());
    }

    if (todosDirty) {
        todosDirty = false;
        AppUi::refreshTodos();
        bleService.updateStoredValues();
        if (bleService.isConnected()) {
            bleService.notifyTodoSync();
            bleService.notifyCompletedLog();
        }
    }

    if (watchfaceDirty) {
        watchfaceDirty = false;
        AppUi::refreshWatchface();
        bleService.updateStoredValues();
        if (bleService.isConnected()) {
            bleService.notifyWatchfaceMeta();
        }
    }

    if (alarmsDirty) {
        alarmsDirty = false;
        if (bleService.isConnected()) {
            bleService.notifyAlarmSync();
        }
    }

    const uint32_t now = millis();
    if (now - lastBatteryNotifyMs > 5000) {
        lastBatteryNotifyMs = now;
        AppUi::refreshStatusBar();
        if (bleService.isConnected()) {
            bleService.notifyDeviceInfo();
        }
    }

    // Always service LVGL, even with the backlight off, so it keeps reading
    // the touch panel. That lets a tap reset LVGL's inactivity timer and wake
    // the screen via displaySleep.loopTick() above.
    lv_task_handler();
    delay(5);
}
