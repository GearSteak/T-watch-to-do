#include "watchface_store.h"
#include "config.h"

#include <FFat.h>
#include <ArduinoJson.h>
#include <cstring>

WatchfaceStore watchfaceStore;

static uint32_t parseColor(const JsonVariant &v, uint32_t fallback) {
    if (v.isNull()) {
        return fallback;
    }
    if (v.is<const char *>()) {
        const char *hex = v.as<const char *>();
        if (hex[0] == '#' && strlen(hex) >= 7) {
            return (uint32_t)strtoul(hex + 1, nullptr, 16);
        }
    }
    if (v.is<uint32_t>()) {
        return v.as<uint32_t>();
    }
    return fallback;
}

static void colorToHex(uint32_t color, char *buf, size_t len) {
    snprintf(buf, len, "#%06lX", (unsigned long)(color & 0xFFFFFF));
}

bool WatchfaceStore::begin() {
    return load();
}

bool WatchfaceStore::load() {
    config_ = WatchfaceConfig();
    if (!FFat.exists(WATCHFACE_FILE)) {
        return save();
    }

    File f = FFat.open(WATCHFACE_FILE, "r");
    if (!f) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, f)) {
        f.close();
        return false;
    }
    f.close();

    config_.preset = doc["preset"] | "classic";
    config_.showDate = doc["showDate"] | true;
    config_.showBattery = doc["showBattery"] | true;
    config_.hasImage = doc["hasImage"] | false;
    config_.bgColor = parseColor(doc["bgColor"], 0x0f0f1a);
    config_.timeColor = parseColor(doc["timeColor"], 0xffffff);
    config_.dateColor = parseColor(doc["dateColor"], 0xaaaaaa);
    return true;
}

bool WatchfaceStore::save() {
    File f = FFat.open(WATCHFACE_FILE, "w");
    if (!f) {
        return false;
    }
    const String json = toJson();
    f.print(json);
    f.close();
    return true;
}

bool WatchfaceStore::clearAll() {
    config_ = WatchfaceConfig();
    if (FFat.exists(WATCHFACE_FILE)) {
        FFat.remove(WATCHFACE_FILE);
    }
    return save();
}

void WatchfaceStore::notifyChange() {
    save();
    if (onChange_) {
        onChange_();
    }
}

void WatchfaceStore::setConfig(const WatchfaceConfig &cfg) {
    config_ = cfg;
    notifyChange();
}

void WatchfaceStore::setHasImage(bool hasImage) {
    if (config_.hasImage == hasImage) {
        return;
    }
    config_.hasImage = hasImage;
    notifyChange();
}

void WatchfaceStore::cyclePreset() {
    if (config_.preset == "classic") {
        config_.preset = "minimal";
    } else if (config_.preset == "minimal") {
        config_.preset = "bold";
    } else {
        config_.preset = "classic";
    }
    notifyChange();
}

bool WatchfaceStore::applyFromJson(const String &json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        return false;
    }

    WatchfaceConfig cfg = config_;
    if (!doc["preset"].isNull()) {
        cfg.preset = doc["preset"].as<String>();
    }
    if (!doc["showDate"].isNull()) {
        cfg.showDate = doc["showDate"];
    }
    if (!doc["showBattery"].isNull()) {
        cfg.showBattery = doc["showBattery"];
    }
    if (!doc["bgColor"].isNull()) {
        cfg.bgColor = parseColor(doc["bgColor"], cfg.bgColor);
    }
    if (!doc["timeColor"].isNull()) {
        cfg.timeColor = parseColor(doc["timeColor"], cfg.timeColor);
    }
    if (!doc["dateColor"].isNull()) {
        cfg.dateColor = parseColor(doc["dateColor"], cfg.dateColor);
    }

    config_ = cfg;
    notifyChange();
    return true;
}

String WatchfaceStore::toJson() const {
    char bg[8], time[8], date[8];
    colorToHex(config_.bgColor, bg, sizeof(bg));
    colorToHex(config_.timeColor, time, sizeof(time));
    colorToHex(config_.dateColor, date, sizeof(date));

    JsonDocument doc;
    doc["preset"] = config_.preset;
    doc["showDate"] = config_.showDate;
    doc["showBattery"] = config_.showBattery;
    doc["hasImage"] = config_.hasImage;
    doc["bgColor"] = bg;
    doc["timeColor"] = time;
    doc["dateColor"] = date;

    String out;
    serializeJson(doc, out);
    return out;
}
