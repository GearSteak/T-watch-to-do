#include "alarm_store.h"
#include "config.h"

#include <FFat.h>
#include <ArduinoJson.h>
#include <algorithm>

AlarmStore alarmStore;

static String makeAlarmId() {
    char buf[17];
    snprintf(buf, sizeof(buf), "%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    return String(buf);
}

static bool alarmLess(const AlarmItem &a, const AlarmItem &b) {
    if (a.hour != b.hour) {
        return a.hour < b.hour;
    }
    return a.minute < b.minute;
}

bool AlarmStore::begin() {
    return load();
}

bool AlarmStore::load() {
    items_.clear();
    if (!FFat.exists(ALARM_FILE)) {
        return save();
    }

    File f = FFat.open(ALARM_FILE, "r");
    if (!f || f.size() == 0) {
        if (f) {
            f.close();
        }
        return save();
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[AlarmStore] JSON parse error: %s\n", err.c_str());
        return false;
    }

    for (JsonObject obj : doc["items"].as<JsonArray>()) {
        AlarmItem item;
        item.id = obj["id"].as<String>();
        item.label = obj["label"].as<String>();
        item.hour = obj["hour"] | 0;
        item.minute = obj["minute"] | 0;
        item.enabled = obj["enabled"] | true;
        item.lastFiredYmd = obj["lastFiredYmd"] | 0U;
        item.deleted = obj["deleted"] | false;
        if (!item.deleted) {
            items_.push_back(item);
        }
    }
    std::sort(items_.begin(), items_.end(), alarmLess);
    return true;
}

bool AlarmStore::save() {
    File f = FFat.open(ALARM_FILE, "w");
    if (!f) {
        return false;
    }

    JsonDocument doc;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (const AlarmItem &item : items_) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = item.id;
        obj["label"] = item.label;
        obj["hour"] = item.hour;
        obj["minute"] = item.minute;
        obj["enabled"] = item.enabled;
        obj["lastFiredYmd"] = item.lastFiredYmd;
        obj["deleted"] = false;
    }

    if (serializeJson(doc, f) == 0) {
        f.close();
        return false;
    }
    f.close();
    return true;
}

void AlarmStore::notifyChange() {
    save();
    if (onChange_) {
        onChange_();
    }
}

AlarmItem *AlarmStore::findById(const String &id) {
    for (AlarmItem &item : items_) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

bool AlarmStore::add(uint8_t hour, uint8_t minute, const String &label) {
    if (items_.size() >= MAX_ALARMS) {
        return false;
    }
    AlarmItem item;
    item.id = makeAlarmId();
    item.hour = hour;
    item.minute = minute;
    item.label = label.substring(0, ALARM_LABEL_MAX);
    items_.push_back(item);
    std::sort(items_.begin(), items_.end(), alarmLess);
    notifyChange();
    return true;
}

bool AlarmStore::remove(const String &id) {
    for (auto it = items_.begin(); it != items_.end(); ++it) {
        if (it->id == id) {
            items_.erase(it);
            notifyChange();
            return true;
        }
    }
    return false;
}

bool AlarmStore::setEnabled(const String &id, bool enabled) {
    AlarmItem *item = findById(id);
    if (!item) {
        return false;
    }
    item->enabled = enabled;
    notifyChange();
    return true;
}

bool AlarmStore::markFired(const String &id, uint32_t ymd) {
    AlarmItem *item = findById(id);
    if (!item) {
        return false;
    }
    item->lastFiredYmd = ymd;
    save();
    return true;
}

bool AlarmStore::mergeFromJson(const String &json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        return false;
    }

    for (JsonObject obj : doc["items"].as<JsonArray>()) {
        const String id = obj["id"].as<String>();
        const bool deleted = obj["deleted"] | false;
        if (deleted) {
            remove(id);
            continue;
        }

        AlarmItem *existing = findById(id);
        if (existing) {
            existing->label = obj["label"].as<String>();
            existing->hour = obj["hour"] | existing->hour;
            existing->minute = obj["minute"] | existing->minute;
            existing->enabled = obj["enabled"] | existing->enabled;
            existing->lastFiredYmd = obj["lastFiredYmd"] | existing->lastFiredYmd;
        } else if (items_.size() < MAX_ALARMS) {
            AlarmItem item;
            item.id = id;
            item.label = obj["label"].as<String>();
            item.hour = obj["hour"] | 0;
            item.minute = obj["minute"] | 0;
            item.enabled = obj["enabled"] | true;
            item.lastFiredYmd = obj["lastFiredYmd"] | 0U;
            items_.push_back(item);
        }
    }

    std::sort(items_.begin(), items_.end(), alarmLess);
    notifyChange();
    return true;
}

bool AlarmStore::clearAll() {
    items_.clear();
    if (FFat.exists(ALARM_FILE)) {
        FFat.remove(ALARM_FILE);
    }
    return save();
}

String AlarmStore::toJson() const {
    JsonDocument doc;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (const AlarmItem &item : items_) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = item.id;
        obj["label"] = item.label;
        obj["hour"] = item.hour;
        obj["minute"] = item.minute;
        obj["enabled"] = item.enabled;
        obj["lastFiredYmd"] = item.lastFiredYmd;
        obj["deleted"] = false;
    }
    String out;
    serializeJson(doc, out);
    return out;
}
