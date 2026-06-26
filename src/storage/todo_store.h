#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>

struct TodoItem {
    String id;
    String text;
    bool done = false;
    uint8_t priority = 0; // 0=none, 1=low, 2=medium, 3=high
    uint8_t repeat = 0; // 0=none, 1=daily
    int sortOrder = 0;
    uint64_t createdAt = 0;
    uint64_t completedAt = 0;
    bool deleted = false;
};

class TodoStore {
public:
    bool begin();
    bool load();
    bool save();

    const std::vector<TodoItem> &items() const { return items_; }

    bool add(const String &text);
    bool toggle(const String &id);
    bool remove(const String &id);
    bool setPriority(const String &id, uint8_t priority);
    bool cyclePriority(const String &id);
    bool move(const String &id, int direction);
    bool mergeFromJson(const String &json);
    bool clearAll();
    void processRepeats();
    String toJson() const;
    String completedLogJson(int limit = 20) const;

    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

private:
    std::vector<TodoItem> items_;
    uint32_t lastDailyResetYmd_ = 0;
    std::function<void()> onChange_;
    void notifyChange();
    TodoItem *findById(const String &id);
};

extern TodoStore todoStore;
