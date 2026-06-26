#include "device_reset.h"
#include "storage/todo_store.h"
#include "storage/watchface_store.h"
#include "services/battery_monitor.h"

#include <esp_system.h>

void DeviceReset::perform() {
    Serial.println("[Reset] Factory reset");
    todoStore.clearAll();
    watchfaceStore.clearAll();
    batteryMonitor.resetPrefs();
    delay(300);
    esp_restart();
}
