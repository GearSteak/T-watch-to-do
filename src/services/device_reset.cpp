#include "device_reset.h"
#include "storage/todo_store.h"
#include "storage/watchface_store.h"
#include "storage/alarm_store.h"
#include "services/battery_monitor.h"

#include <esp_system.h>

void DeviceReset::restart() {
    Serial.println("[Reset] Restart");
    delay(200);
    esp_restart();
}

void DeviceReset::factoryReset() {
    Serial.println("[Reset] Factory reset");
    todoStore.clearAll();
    watchfaceStore.clearAll();
    alarmStore.clearAll();
    batteryMonitor.resetPrefs();
    delay(300);
    esp_restart();
}
