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

bool TodoStore::mergeFromJson(const String &json) {
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

        TodoItem *existing = findById(id);
        if (existing) {
            existing->text = obj["text"].as<String>();
            existing->done = obj["done"] | false;
            existing->priority = obj["priority"] | existing->priority;
            existing->repeat = obj["repeat"] | existing->repeat;
            existing->sortOrder = obj["sortOrder"] | existing->sortOrder;
            existing->completedAt = obj["completedAt"] | 0ULL;
        } else if (items_.size() < MAX_TODOS) {
            TodoItem item;
            item.id = id;
            item.text = obj["text"].as<String>();
            item.done = obj["done"] | false;
            item.priority = obj["priority"] | 0;
            item.repeat = obj["repeat"] | 0;
            item.sortOrder = obj["sortOrder"] | nextSortOrder(items_);
            item.createdAt = obj["createdAt"] | (uint64_t)time(nullptr) * 1000ULL;
            item.completedAt = obj["completedAt"] | 0ULL;
            items_.push_back(item);
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
    const uint32_t today = currentYmd();
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
        if (item.repeat == TODO_REPEAT_DAILY && item.done) {
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
