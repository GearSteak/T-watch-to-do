#include "alarm_service.h"
#include "storage/alarm_store.h"

#include <time.h>

AlarmService alarmService;

static uint32_t currentYmd() {
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return (uint32_t)((t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday);
}

void AlarmService::begin() {
    lastCheckedHm_ = 0xFFFF;
}

void AlarmService::loopTick() {
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    const uint16_t hm = (uint16_t)(t.tm_hour * 100 + t.tm_min);
    if (hm == lastCheckedHm_) {
        return;
    }
    lastCheckedHm_ = hm;

    const uint32_t today = currentYmd();
    for (const AlarmItem &item : alarmStore.items()) {
        if (!item.enabled) {
            continue;
        }
        if (item.hour != (uint8_t)t.tm_hour || item.minute != (uint8_t)t.tm_min) {
            continue;
        }
        if (item.lastFiredYmd == today) {
            continue;
        }

        alarmStore.markFired(item.id, today);
        if (onFire_) {
            onFire_(item.label);
        }
    }
}
