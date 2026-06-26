#include <LilyGoLib.h>
#include <LV_Helper.h>

#include "config.h"
#include "ble/ble_service.h"
#include "storage/todo_store.h"
#include "storage/watchface_store.h"
#include "services/battery_monitor.h"
#include "services/time_service.h"
#include "services/display_sleep.h"
#include "ui/app_ui.h"

static uint32_t lastBatteryNotifyMs = 0;
static volatile bool todosDirty = false;
static volatile bool watchfaceDirty = false;

static void onTodoChanged() {
    todosDirty = true;
}

static void onWatchfaceChanged() {
    watchfaceDirty = true;
}

static void onBatteryAlert(int level, int percent) {
    AppUi::showBatteryAlert(level, percent);
    bleService.notifyBatteryAlert(level, percent);
    bleService.notifyDeviceInfo();
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

    batteryMonitor.begin();
    batteryMonitor.setAlertCallback(onBatteryAlert);

    bleService.setTodoChangeCallback(onTodoChanged);
    bleService.setWatchfaceChangeCallback(onWatchfaceChanged);
    bleService.begin();

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

    int16_t touchX = 0;
    int16_t touchY = 0;
    if (instance.getPoint(&touchX, &touchY, 1)) {
        displaySleep.onTouch();
    }

    displaySleep.loopTick(AppUi::isAlertVisible());

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

    const uint32_t now = millis();
    if (now - lastBatteryNotifyMs > 5000) {
        lastBatteryNotifyMs = now;
        AppUi::refreshStatusBar();
        if (bleService.isConnected()) {
            bleService.notifyDeviceInfo();
        }
    }

    if (!displaySleep.isAsleep()) {
        lv_task_handler();
    }
    delay(5);
}
