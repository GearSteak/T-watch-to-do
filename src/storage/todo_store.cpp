#include "todo_store.h"
#include "config.h"

#include <FFat.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <time.h>

TodoStore todoStore;

static uint32_t currentYmd() {
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return (uint32_t)((t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday);
}

static uint32_t ymdFromMs(uint64_t ms) {
    const time_t sec = (time_t)(ms / 1000ULL);
    struct tm t;
    localtime_r(&sec, &t);
    return (uint32_t)((t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday);
}

static time_t ymdToTime(uint32_t ymd) {
    struct tm t = {};
    t.tm_year = (int)(ymd / 10000) - 1900;
    t.tm_mon = (int)((ymd / 100) % 100) - 1;
    t.tm_mday = (int)(ymd % 100);
    t.tm_hour = 12;
    return mktime(&t);
}

static int daysBetweenYmd(uint32_t fromYmd, uint32_t toYmd) {
    if (toYmd <= fromYmd) {
        return 0;
    }
    const double diff = difftime(ymdToTime(toYmd), ymdToTime(fromYmd));
    return (int)(diff / 86400.0);
}

static int daysInMonth(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 31;
    }
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month - 1];
}

static int monthlyTriggerDay(const struct tm &todayTm, uint8_t repeatMonthDay) {
    if (repeatMonthDay < 1 || repeatMonthDay > 31) {
        return -1;
    }
    const int year = todayTm.tm_year + 1900;
    const int month = todayTm.tm_mon + 1;
    const int dim = daysInMonth(year, month);
    return repeatMonthDay > (uint8_t)dim ? dim : repeatMonthDay;
}

static int nextSortOrder(const std::vector<TodoItem> &items) {
    int maxOrder = -1;
    for (const TodoItem &item : items) {
        if (item.sortOrder > maxOrder) {
            maxOrder = item.sortOrder;
        }
    }
    return maxOrder + 1;
}

static bool todoLess(const TodoItem &a, const TodoItem &b) {
    if (a.done != b.done) {
        return a.done < b.done;
    }
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    if (a.sortOrder != b.sortOrder) {
        return a.sortOrder < b.sortOrder;
    }
    return a.createdAt < b.createdAt;
}

static void sortTodoItems(std::vector<TodoItem> &items) {
    std::sort(items.begin(), items.end(), todoLess);
}

static String makeId() {
    char buf[17];
    snprintf(buf, sizeof(buf), "%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    return String(buf);
}

bool TodoStore::begin() {
    if (!FFat.begin(true)) {
        Serial.println("[TodoStore] FFat mount failed");
        return false;
    }
    return load();
}

bool TodoStore::load() {
    items_.clear();
    if (!FFat.exists(TODO_FILE)) {
        return save();
    }

    File f = FFat.open(TODO_FILE, "r");
    if (!f) {
        return false;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[TodoStore] JSON parse error: %s\n", err.c_str());
        return false;
    }

    lastDailyResetYmd_ = doc["lastDailyResetYmd"] | 0U;

    for (JsonObject obj : doc["items"].as<JsonArray>()) {
        TodoItem item;
        item.id = obj["id"].as<String>();
        item.text = obj["text"].as<String>();
        item.done = obj["done"] | false;
        item.priority = obj["priority"] | 0;
        item.repeat = obj["repeat"] | 0;
        item.repeatWeekday = obj["repeatWeekday"] | 0;
        item.repeatIntervalDays = obj["repeatIntervalDays"] | 0;
        item.repeatMonthDay = obj["repeatMonthDay"] | 0;
        item.remindMinute = obj["remindMinute"] | 0xFFFF;
        item.lastRemindedYmd = obj["lastRemindedYmd"] | 0U;
        item.sortOrder = obj["sortOrder"] | 0;
        item.createdAt = obj["createdAt"] | 0ULL;
        item.completedAt = obj["completedAt"] | 0ULL;
        item.deleted = obj["deleted"] | false;
        if (!item.deleted) {
            items_.push_back(item);
        }
    }
    sortTodoItems(items_);
    return true;
}

bool TodoStore::save() {
    File f = FFat.open(TODO_FILE, "w");
    if (!f) {
        return false;
    }

    JsonDocument doc;
    doc["lastDailyResetYmd"] = lastDailyResetYmd_;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (const TodoItem &item : items_) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = item.id;
        obj["text"] = item.text;
        obj["done"] = item.done;
        obj["priority"] = item.priority;
        obj["repeat"] = item.repeat;
        obj["repeatWeekday"] = item.repeatWeekday;
        obj["repeatIntervalDays"] = item.repeatIntervalDays;
        obj["repeatMonthDay"] = item.repeatMonthDay;
        obj["remindMinute"] = item.remindMinute;
        obj["lastRemindedYmd"] = item.lastRemindedYmd;
        obj["sortOrder"] = item.sortOrder;
        obj["createdAt"] = item.createdAt;
        obj["completedAt"] = item.completedAt;
        obj["deleted"] = false;
    }

    if (serializeJson(doc, f) == 0) {
        f.close();
        return false;
    }
    f.close();
    return true;
}

void TodoStore::notifyChange() {
    save();
    if (onChange_) {
        onChange_();
    }
}

TodoItem *TodoStore::findById(const String &id) {
    for (TodoItem &item : items_) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

bool TodoStore::add(const String &text) {
    if (text.isEmpty() || items_.size() >= MAX_TODOS) {
        return false;
    }
    TodoItem item;
    item.id = makeId();
    item.text = text.substring(0, MAX_TODO_TEXT);
    item.sortOrder = nextSortOrder(items_);
    item.createdAt = (uint64_t)time(nullptr) * 1000ULL;
    items_.push_back(item);
    notifyChange();
    return true;
}

bool TodoStore::toggle(const String &id) {
    TodoItem *item = findById(id);
    if (!item) {
        return false;
    }
    item->done = !item->done;
    item->completedAt = item->done ? (uint64_t)time(nullptr) * 1000ULL : 0;
    notifyChange();
    return true;
}

bool TodoStore::setPriority(const String &id, uint8_t priority) {
    TodoItem *item = findById(id);
    if (!item || priority > 3) {
        return false;
    }
    item->priority = priority;
    sortTodoItems(items_);
    notifyChange();
    return true;
}

bool TodoStore::cyclePriority(const String &id) {
    TodoItem *item = findById(id);
    if (!item) {
        return false;
    }
    item->priority = (item->priority + 1) % 4;
    sortTodoItems(items_);
    notifyChange();
    return true;
}

bool TodoStore::move(const String &id, int direction) {
    if (direction == 0) {
        return false;
    }

    std::vector<TodoItem *> active;
    active.reserve(items_.size());
    for (TodoItem &item : items_) {
        if (!item.done) {
            active.push_back(&item);
        }
    }
    std::sort(active.begin(), active.end(), [](const TodoItem *a, const TodoItem *b) {
        return todoLess(*a, *b);
    });

    int idx = -1;
    for (size_t i = 0; i < active.size(); ++i) {
        if (active[i]->id == id) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0) {
        return false;
    }

    const int swapIdx = idx + (direction < 0 ? -1 : 1);
    if (swapIdx < 0 || swapIdx >= (int)active.size()) {
        return false;
    }

    const int tmp = active[idx]->sortOrder;
    active[idx]->sortOrder = active[swapIdx]->sortOrder;
    active[swapIdx]->sortOrder = tmp;
    sortTodoItems(items_);
    notifyChange();
    return true;
}

bool TodoStore::remove(const String &id) {
    for (auto it = items_.begin(); it != items_.end(); ++it) {
        if (it->id == id) {
            items_.erase(it);
            notifyChange();
            return true;
        }
    }
    return false;
}

bool TodoStore::mergeFromJson(const String &json, size_t *addedCount) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        return false;
    }

    if (addedCount) {
        *addedCount = 0;
    }

    const bool fullSync = doc["fullSync"] | false;
    std::vector<String> incomingIds;
    incomingIds.reserve(16);

    for (JsonObject obj : doc["items"].as<JsonArray>()) {
        const String id = obj["id"].as<String>();
        if (id.isEmpty()) {
            continue;
        }
        incomingIds.push_back(id);

        const bool deleted = obj["deleted"] | false;
        if (deleted) {
            remove(id);
            continue;
        }

        TodoItem *existing = findById(id);
        if (existing) {
            existing->text = obj["text"].as<String>();
            existing->done = obj["done"] | false;
            existing->priority = obj["priority"] | existing->priority;
            existing->repeat = obj["repeat"] | existing->repeat;
            existing->repeatWeekday = obj["repeatWeekday"] | existing->repeatWeekday;
            existing->repeatIntervalDays = obj["repeatIntervalDays"] | existing->repeatIntervalDays;
            existing->repeatMonthDay = obj["repeatMonthDay"] | existing->repeatMonthDay;
            if (obj["remindMinute"].is<uint16_t>()) {
                const uint16_t newRemind = obj["remindMinute"].as<uint16_t>();
                if (newRemind != existing->remindMinute) {
                    existing->remindMinute = newRemind;
                    existing->lastRemindedYmd = 0; // allow the new time to fire today
                }
            }
            existing->sortOrder = obj["sortOrder"] | existing->sortOrder;
            existing->completedAt = obj["completedAt"] | 0ULL;
        } else if (items_.size() < MAX_TODOS) {
            TodoItem item;
            item.id = id;
            item.text = obj["text"].as<String>();
            item.done = obj["done"] | false;
            item.priority = obj["priority"] | 0;
            item.repeat = obj["repeat"] | 0;
            item.repeatWeekday = obj["repeatWeekday"] | 0;
            item.repeatIntervalDays = obj["repeatIntervalDays"] | 0;
            item.repeatMonthDay = obj["repeatMonthDay"] | 0;
            item.remindMinute = obj["remindMinute"] | 0xFFFF;
            item.sortOrder = obj["sortOrder"] | nextSortOrder(items_);
            item.createdAt = obj["createdAt"] | (uint64_t)time(nullptr) * 1000ULL;
            item.completedAt = obj["completedAt"] | 0ULL;
            items_.push_back(item);
            if (addedCount) {
                (*addedCount)++;
            }
        }
    }

    if (fullSync && !incomingIds.empty()) {
        for (size_t i = 0; i < items_.size();) {
            bool keep = false;
            for (const String &incomingId : incomingIds) {
                if (items_[i].id == incomingId) {
                    keep = true;
                    break;
                }
            }
            if (!keep) {
                items_.erase(items_.begin() + (int)i);
            } else {
                ++i;
            }
        }
    }

    sortTodoItems(items_);
    notifyChange();
    return true;
}

bool TodoStore::clearAll() {
    items_.clear();
    lastDailyResetYmd_ = 0;
    if (FFat.exists(TODO_FILE)) {
        FFat.remove(TODO_FILE);
    }
    return save();
}

void TodoStore::processRepeats() {
    const time_t now = time(nullptr);
    struct tm todayTm;
    localtime_r(&now, &todayTm);
    const uint32_t today = currentYmd();
    const int wday = todayTm.tm_wday;

    if (lastDailyResetYmd_ == 0) {
        lastDailyResetYmd_ = today;
        save();
        return;
    }
    if (today <= lastDailyResetYmd_) {
        return;
    }

    bool changed = false;
    for (TodoItem &item : items_) {
        if (!item.done || item.repeat == TODO_REPEAT_NONE) {
            continue;
        }

        bool shouldReset = false;
        switch (item.repeat) {
        case TODO_REPEAT_DAILY:
            shouldReset = true;
            break;
        case TODO_REPEAT_WEEKLY:
            if (wday == item.repeatWeekday) {
                shouldReset = true;
            }
            break;
        case TODO_REPEAT_INTERVAL:
            if (item.repeatIntervalDays >= 2 && item.completedAt > 0) {
                const uint32_t completedYmd = ymdFromMs(item.completedAt);
                if (daysBetweenYmd(completedYmd, today) >= item.repeatIntervalDays) {
                    shouldReset = true;
                }
            }
            break;
        case TODO_REPEAT_MONTHLY: {
            const int triggerDay = monthlyTriggerDay(todayTm, item.repeatMonthDay);
            if (triggerDay > 0 && todayTm.tm_mday == triggerDay) {
                shouldReset = true;
            }
            break;
        }
        default:
            break;
        }

        if (shouldReset) {
            item.done = false;
            item.completedAt = 0;
            changed = true;
        }
    }

    lastDailyResetYmd_ = today;
    if (changed) {
        notifyChange();
    } else {
        save();
    }
}

bool TodoStore::dueReminder(String &outText) {
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    const uint16_t nowMinute = (uint16_t)(t.tm_hour * 60 + t.tm_min);
    const uint32_t today = currentYmd();

    for (TodoItem &item : items_) {
        if (item.done || item.remindMinute > 1439) {
            continue;
        }
        if (item.remindMinute != nowMinute || item.lastRemindedYmd == today) {
            continue;
        }
        item.lastRemindedYmd = today;
        save();
        outText = item.text;
        return true;
    }
    return false;
}

String TodoStore::toJson() const {
    JsonDocument doc;
    doc["lastDailyResetYmd"] = lastDailyResetYmd_;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (const TodoItem &item : items_) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = item.id;
        obj["text"] = item.text;
        obj["done"] = item.done;
        obj["priority"] = item.priority;
        obj["repeat"] = item.repeat;
        obj["repeatWeekday"] = item.repeatWeekday;
        obj["repeatIntervalDays"] = item.repeatIntervalDays;
        obj["repeatMonthDay"] = item.repeatMonthDay;
        obj["remindMinute"] = item.remindMinute;
        obj["lastRemindedYmd"] = item.lastRemindedYmd;
        obj["sortOrder"] = item.sortOrder;
        obj["createdAt"] = item.createdAt;
        obj["completedAt"] = item.completedAt;
        obj["deleted"] = false;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

String TodoStore::completedLogJson(int limit) const {
    JsonDocument doc;
    JsonArray arr = doc["items"].to<JsonArray>();
    int count = 0;
    for (auto it = items_.rbegin(); it != items_.rend() && count < limit; ++it) {
        if (!it->done) {
            continue;
        }
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = it->id;
        obj["text"] = it->text;
        obj["completedAt"] = it->completedAt;
        ++count;
    }
    String out;
    serializeJson(doc, out);
    return out;
}
