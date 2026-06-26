#pragma once

#include <Arduino.h>
#include <functional>

struct WatchfaceConfig {
    String preset = "classic";
    bool showDate = true;
    bool showBattery = true;
    bool hasImage = false;
    uint32_t bgColor = 0x0f0f1a;
    uint32_t timeColor = 0xffffff;
    uint32_t dateColor = 0xaaaaaa;
};

class WatchfaceStore {
public:
    bool begin();
    bool load();
    bool save();
    bool clearAll();
    bool applyFromJson(const String &json);
    String toJson() const;

    const WatchfaceConfig &config() const { return config_; }
    void setConfig(const WatchfaceConfig &cfg);
    void cyclePreset();
    void setHasImage(bool hasImage);

    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

private:
    WatchfaceConfig config_;
    std::function<void()> onChange_;
    void notifyChange();
};

extern WatchfaceStore watchfaceStore;
