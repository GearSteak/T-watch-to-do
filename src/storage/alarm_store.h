#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>

struct AlarmItem {
    String id;
    String label;
    uint8_t hour = 0;
    uint8_t minute = 0;
    bool enabled = true;
    uint32_t lastFiredYmd = 0;
    bool deleted = false;
};

class AlarmStore {
public:
    bool begin();
    bool load();
    bool save();

    const std::vector<AlarmItem> &items() const { return items_; }

    bool add(uint8_t hour, uint8_t minute, const String &label);
    bool remove(const String &id);
    bool setEnabled(const String &id, bool enabled);
    bool markFired(const String &id, uint32_t ymd);
    bool mergeFromJson(const String &json);
    bool clearAll();
    String toJson() const;

    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

private:
    std::vector<AlarmItem> items_;
    std::function<void()> onChange_;
    void notifyChange();
    AlarmItem *findById(const String &id);
};

extern AlarmStore alarmStore;
