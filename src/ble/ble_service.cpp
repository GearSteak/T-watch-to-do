#include "ble_service.h"
#include "config.h"
#include "storage/todo_store.h"
#include "storage/watchface_store.h"
#include "storage/alarm_store.h"
#include "services/battery_monitor.h"
#include "services/time_service.h"

#include <NimBLEDevice.h>
#include <FFat.h>
#include <esp_heap_caps.h>
#include <cstring>

BleService bleService;

static NimBLECharacteristic *chrDeviceInfo = nullptr;
static NimBLECharacteristic *chrTimeSync = nullptr;
static NimBLECharacteristic *chrTodoSync = nullptr;
static NimBLECharacteristic *chrBatteryAlert = nullptr;
static NimBLECharacteristic *chrCompletedLog = nullptr;
static NimBLECharacteristic *chrWatchfaceMeta = nullptr;
static NimBLECharacteristic *chrWatchfaceImage = nullptr;
static NimBLECharacteristic *chrAlarmSync = nullptr;

static String buildDeviceInfoJson() {
    const uint64_t total = FFat.totalBytes();
    const uint64_t used = FFat.usedBytes();
    return String("{\"fw\":\"") + FIRMWARE_VERSION +
           "\",\"battery\":" + batteryMonitor.state().percent +
           ",\"charging\":" + (batteryMonitor.state().charging ? "true" : "false") +
           ",\"storageFree\":" + (total - used) + "}";
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
        bleService.setConnected(true);
        Serial.println("[BLE] Client connected");
        // Never call notify() from the BLE host task — it triggers NVS writes
        // for CCCD persistence and can panic (LoadStoreAlignment). Defer to loop().
        bleService.scheduleConnectNotify();
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override {
        bleService.setConnected(false);
        Serial.println("[BLE] Client disconnected");
        NimBLEDevice::startAdvertising();
    }
};

class TimeSyncCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
        const std::string &value = characteristic->getValue();
        if (value.size() < 8) {
            return;
        }

        int64_t unixMs = 0;
        memcpy(&unixMs, value.data(), 8);
        int32_t tzOffset = 0;
        if (value.size() >= 12) {
            memcpy(&tzOffset, value.data() + 8, 4);
        }

        TimeService::setFromUnixMs(unixMs, tzOffset);
        Serial.printf("[BLE] Time set to %lld (tz %+d min)\n", (long long)unixMs, tzOffset);
        bleService.scheduleDeviceInfoNotify();
    }
};

// NOTE: We intentionally do NOT set the characteristic value inside onRead.
// Re-setting the value during a multi-packet "long read" truncates the data in
// NimBLE. Instead the value is kept current via update*Value()/notify*() so the
// stored buffer always serves complete reads.
class TodoSyncCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
        const std::string &value = characteristic->getValue();
        if (value.empty()) {
            return;
        }
        const uint8_t *bytes = (const uint8_t *)value.data();
        const uint8_t op = bytes[0];
        const size_t len = value.size();

        switch (op) {
        case TODO_OP_DL_PREPARE:
            // Snapshot the list and expose its total length for the client.
            bleService.todoDownloadPrepare();
            break;
        case TODO_OP_DL_PAGE:
            if (len >= 5) {
                uint32_t offset = 0;
                memcpy(&offset, bytes + 1, 4);
                bleService.todoDownloadPage(offset);
            }
            break;
        case TODO_OP_UPLOAD_BEGIN:
            if (len >= 5) {
                uint32_t total = 0;
                memcpy(&total, bytes + 1, 4);
                bleService.todoUploadBegin(total);
            }
            break;
        case TODO_OP_UPLOAD_DATA:
            if (len >= 5) {
                uint32_t offset = 0;
                memcpy(&offset, bytes + 1, 4);
                bleService.todoUploadData(offset, bytes + 5, len - 5);
            }
            break;
        case TODO_OP_UPLOAD_COMMIT:
            bleService.todoUploadCommit();
            break;
        default:
            // Legacy path: a full JSON object written directly (small lists).
            if (op == '{') {
                bleService.queueTodoWrite(value);
            }
            break;
        }
    }
};

class AlarmSyncCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
        const std::string &value = characteristic->getValue();
        if (value.empty()) {
            return;
        }
        const uint8_t *bytes = (const uint8_t *)value.data();
        const uint8_t op = bytes[0];
        const size_t len = value.size();

        switch (op) {
        case TODO_OP_DL_PREPARE:
            bleService.alarmDownloadPrepare();
            break;
        case TODO_OP_DL_PAGE:
            if (len >= 5) {
                uint32_t offset = 0;
                memcpy(&offset, bytes + 1, 4);
                bleService.alarmDownloadPage(offset);
            }
            break;
        case TODO_OP_UPLOAD_BEGIN:
            if (len >= 5) {
                uint32_t total = 0;
                memcpy(&total, bytes + 1, 4);
                bleService.alarmUploadBegin(total);
            }
            break;
        case TODO_OP_UPLOAD_DATA:
            if (len >= 5) {
                uint32_t offset = 0;
                memcpy(&offset, bytes + 1, 4);
                bleService.alarmUploadData(offset, bytes + 5, len - 5);
            }
            break;
        case TODO_OP_UPLOAD_COMMIT:
            bleService.alarmUploadCommit();
            break;
        default:
            if (op == '{') {
                bleService.queueAlarmWrite(value);
            }
            break;
        }
    }
};

class WatchfaceMetaCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
        const std::string &value = characteristic->getValue();
        if (value.empty()) {
            return;
        }
        bleService.queueWatchfaceWrite(value);
    }
};

class WatchfaceImageCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
        const std::string &value = characteristic->getValue();
        if (value.empty()) {
            return;
        }
        const uint8_t *bytes = (const uint8_t *)value.data();
        const uint8_t op = bytes[0];
        const size_t len = value.size();

        switch (op) {
        case WF_IMG_OP_BEGIN: {
            if (len >= 5) {
                uint32_t total = 0;
                memcpy(&total, bytes + 1, 4);
                bleService.imageBegin(total);
            }
            break;
        }
        case WF_IMG_OP_DATA: {
            if (len >= 5) {
                uint32_t offset = 0;
                memcpy(&offset, bytes + 1, 4);
                bleService.imageData(offset, bytes + 5, len - 5);
            }
            break;
        }
        case WF_IMG_OP_COMMIT:
            bleService.imageCommit();
            break;
        case WF_IMG_OP_CLEAR:
            bleService.imageClear();
            break;
        default:
            break;
        }
    }
};

bool BleService::begin() {
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    // Larger MTU so todo/watchface JSON fits in a single packet for most lists
    // and notifications aren't truncated.
    NimBLEDevice::setMTU(517);

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService *service = server->createService(SVC_UUID);

    chrDeviceInfo = service->createCharacteristic(
        CHR_DEVICE_INFO,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    chrTimeSync = service->createCharacteristic(
        CHR_TIME_SYNC,
        NIMBLE_PROPERTY::WRITE);
    chrTodoSync = service->createCharacteristic(
        CHR_TODO_SYNC,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    chrBatteryAlert = service->createCharacteristic(
        CHR_BATTERY_ALERT,
        NIMBLE_PROPERTY::NOTIFY);
    chrCompletedLog = service->createCharacteristic(
        CHR_COMPLETED_LOG,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    chrWatchfaceMeta = service->createCharacteristic(
        CHR_WATCHFACE_META,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    chrWatchfaceImage = service->createCharacteristic(
        CHR_WATCHFACE_IMAGE,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    chrAlarmSync = service->createCharacteristic(
        CHR_ALARM_SYNC,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);

    chrTimeSync->setCallbacks(new TimeSyncCallbacks());
    chrTodoSync->setCallbacks(new TodoSyncCallbacks());
    chrAlarmSync->setCallbacks(new AlarmSyncCallbacks());
    chrWatchfaceMeta->setCallbacks(new WatchfaceMetaCallbacks());
    chrWatchfaceImage->setCallbacks(new WatchfaceImageCallbacks());

    // Seed the small stored values so reads return complete data immediately.
    // The todo list is NOT stored as one value (it can exceed the 512-byte GATT
    // limit); it is transferred via the paged download protocol instead.
    chrDeviceInfo->setValue(buildDeviceInfoJson().c_str());
    chrWatchfaceMeta->setValue(watchfaceStore.toJson().c_str());

    service->start();

    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SVC_UUID);
    advertising->setName(DEVICE_NAME);
    advertising->start();

    Serial.println("[BLE] Advertising as " DEVICE_NAME);
    return true;
}

void BleService::updateStoredValues() {
    // Only the small values are stored directly. The todo list is served via
    // the paged download protocol, so it is never set as a single value here.
    if (chrWatchfaceMeta) {
        chrWatchfaceMeta->setValue(watchfaceStore.toJson().c_str());
    }
    if (chrDeviceInfo) {
        chrDeviceInfo->setValue(buildDeviceInfoJson().c_str());
    }
}

void BleService::queueTodoWrite(const std::string &json) {
    pendingTodoJson_ = json;
    pendingTodoWrite_ = true;
}

void BleService::queueWatchfaceWrite(const std::string &json) {
    pendingWatchfaceJson_ = json;
    pendingWatchfaceWrite_ = true;
}

void BleService::queueAlarmWrite(const std::string &json) {
    pendingAlarmJson_ = json;
    pendingAlarmWrite_ = true;
}

void BleService::todoDownloadPrepare() {
    todoDownloadBuf_ = std::string(todoStore.toJson().c_str());
    const uint32_t total = (uint32_t)todoDownloadBuf_.size();
    uint8_t header[4];
    memcpy(header, &total, 4);
    if (chrTodoSync) {
        chrTodoSync->setValue(header, sizeof(header));
    }
}

void BleService::todoDownloadPage(uint32_t offset) {
    if (!chrTodoSync) {
        return;
    }
    if (offset >= todoDownloadBuf_.size()) {
        chrTodoSync->setValue((const uint8_t *)"", 0);
        return;
    }
    size_t len = todoDownloadBuf_.size() - offset;
    if (len > TODO_PAGE_SIZE) {
        len = TODO_PAGE_SIZE;
    }
    chrTodoSync->setValue((const uint8_t *)todoDownloadBuf_.data() + offset, len);
}

void BleService::todoUploadBegin(uint32_t totalSize) {
    if (totalSize == 0 || totalSize > TODO_UPLOAD_MAX) {
        return;
    }
    if (todoUpBuf_) {
        free(todoUpBuf_);
        todoUpBuf_ = nullptr;
    }
    todoUpBuf_ = (uint8_t *)malloc(totalSize + 1);
    todoUpExpected_ = todoUpBuf_ ? totalSize : 0;
}

void BleService::todoUploadData(uint32_t offset, const uint8_t *data, size_t len) {
    if (!todoUpBuf_ || todoUpExpected_ == 0) {
        return;
    }
    if (offset + len > todoUpExpected_) {
        return;
    }
    memcpy(todoUpBuf_ + offset, data, len);
}

void BleService::todoUploadCommit() {
    if (!todoUpBuf_ || todoUpExpected_ == 0) {
        return;
    }
    todoUpBuf_[todoUpExpected_] = 0;
    queueTodoWrite(std::string((const char *)todoUpBuf_, todoUpExpected_));
    free(todoUpBuf_);
    todoUpBuf_ = nullptr;
    todoUpExpected_ = 0;
}

void BleService::alarmDownloadPrepare() {
    alarmDownloadBuf_ = std::string(alarmStore.toJson().c_str());
    const uint32_t total = (uint32_t)alarmDownloadBuf_.size();
    uint8_t header[4];
    memcpy(header, &total, 4);
    if (chrAlarmSync) {
        chrAlarmSync->setValue(header, sizeof(header));
    }
}

void BleService::alarmDownloadPage(uint32_t offset) {
    if (!chrAlarmSync) {
        return;
    }
    if (offset >= alarmDownloadBuf_.size()) {
        chrAlarmSync->setValue((const uint8_t *)"", 0);
        return;
    }
    size_t len = alarmDownloadBuf_.size() - offset;
    if (len > TODO_PAGE_SIZE) {
        len = TODO_PAGE_SIZE;
    }
    chrAlarmSync->setValue((const uint8_t *)alarmDownloadBuf_.data() + offset, len);
}

void BleService::alarmUploadBegin(uint32_t totalSize) {
    if (totalSize == 0 || totalSize > ALARM_UPLOAD_MAX) {
        return;
    }
    if (alarmUpBuf_) {
        free(alarmUpBuf_);
        alarmUpBuf_ = nullptr;
    }
    alarmUpBuf_ = (uint8_t *)malloc(totalSize + 1);
    alarmUpExpected_ = alarmUpBuf_ ? totalSize : 0;
}

void BleService::alarmUploadData(uint32_t offset, const uint8_t *data, size_t len) {
    if (!alarmUpBuf_ || alarmUpExpected_ == 0) {
        return;
    }
    if (offset + len > alarmUpExpected_) {
        return;
    }
    memcpy(alarmUpBuf_ + offset, data, len);
}

void BleService::alarmUploadCommit() {
    if (!alarmUpBuf_ || alarmUpExpected_ == 0) {
        return;
    }
    alarmUpBuf_[alarmUpExpected_] = 0;
    queueAlarmWrite(std::string((const char *)alarmUpBuf_, alarmUpExpected_));
    free(alarmUpBuf_);
    alarmUpBuf_ = nullptr;
    alarmUpExpected_ = 0;
}

void BleService::imageBegin(uint32_t totalSize) {
    if (totalSize == 0 || totalSize > WATCHFACE_IMAGE_BYTES) {
        return;
    }
    if (!imageBuf_) {
        imageBuf_ = (uint8_t *)heap_caps_malloc(WATCHFACE_IMAGE_BYTES, MALLOC_CAP_SPIRAM);
        if (!imageBuf_) {
            imageBuf_ = (uint8_t *)malloc(WATCHFACE_IMAGE_BYTES);
        }
    }
    imageExpected_ = totalSize;
    imageReceived_ = 0;
    Serial.printf("[BLE] Image upload begin: %u bytes\n", (unsigned)totalSize);
}

void BleService::imageData(uint32_t offset, const uint8_t *data, size_t len) {
    if (!imageBuf_ || len == 0) {
        return;
    }
    if (offset + len > WATCHFACE_IMAGE_BYTES) {
        return;
    }
    memcpy(imageBuf_ + offset, data, len);
    imageReceived_ += len;
}

void BleService::imageCommit() {
    pendingImageCommit_ = true;
}

void BleService::imageClear() {
    pendingImageClear_ = true;
}

void BleService::scheduleConnectNotify() {
    pendingConnectNotify_ = true;
}

void BleService::scheduleDeviceInfoNotify() {
    pendingDeviceInfoNotify_ = true;
}

void BleService::loopTick() {
    if (pendingTodoWrite_) {
        pendingTodoWrite_ = false;
        const String json(pendingTodoJson_.c_str());
        pendingTodoJson_.clear();
        size_t added = 0;
        todoStore.mergeFromJson(json, &added);
        if (added > 0) {
            pendingTodoAddedNotify_ = true;
        }
    }

    if (pendingWatchfaceWrite_) {
        pendingWatchfaceWrite_ = false;
        const String json(pendingWatchfaceJson_.c_str());
        pendingWatchfaceJson_.clear();
        watchfaceStore.applyFromJson(json);
    }

    if (pendingAlarmWrite_) {
        pendingAlarmWrite_ = false;
        const String json(pendingAlarmJson_.c_str());
        pendingAlarmJson_.clear();
        alarmStore.mergeFromJson(json);
    }

    if (pendingImageCommit_) {
        pendingImageCommit_ = false;
        if (imageBuf_ && imageExpected_ > 0) {
            File f = FFat.open(WATCHFACE_IMAGE_FILE, "w");
            if (f) {
                f.write(imageBuf_, imageExpected_);
                f.close();
                watchfaceStore.setHasImage(true);
                Serial.printf("[BLE] Image committed (%u/%u bytes)\n",
                              (unsigned)imageReceived_, (unsigned)imageExpected_);
            } else {
                Serial.println("[BLE] Image commit failed to open file");
            }
        }
        if (imageBuf_) {
            free(imageBuf_);
            imageBuf_ = nullptr;
        }
        imageExpected_ = 0;
        imageReceived_ = 0;
    }

    if (pendingImageClear_) {
        pendingImageClear_ = false;
        if (FFat.exists(WATCHFACE_IMAGE_FILE)) {
            FFat.remove(WATCHFACE_IMAGE_FILE);
        }
        watchfaceStore.setHasImage(false);
        Serial.println("[BLE] Image cleared");
    }

    if (pendingConnectNotify_) {
        pendingConnectNotify_ = false;
        if (connected_) {
            notifyDeviceInfo();
            notifyTodoSync();
            notifyWatchfaceMeta();
            notifyAlarmSync();
        }
    }

    if (pendingDeviceInfoNotify_) {
        pendingDeviceInfoNotify_ = false;
        notifyDeviceInfo();
    }
}

void BleService::notifyDeviceInfo() {
    if (!connected_ || !chrDeviceInfo) {
        return;
    }
    const String json = buildDeviceInfoJson();
    chrDeviceInfo->setValue(json.c_str());
    chrDeviceInfo->notify();
}

void BleService::notifyTodoSync() {
    if (!connected_ || !chrTodoSync) {
        return;
    }
    // Send only a 1-byte "changed" ping. The list itself is fetched by the
    // client via the paged download protocol (a GATT value can't exceed 512
    // bytes, so we never push the full list in one notification).
    const uint8_t ping = TODO_PING_CHANGED;
    chrTodoSync->notify(&ping, 1);
}

void BleService::notifyCompletedLog() {
    // Completed items are derived from the todo list on the client side, so
    // there is nothing to push separately. Kept for API compatibility.
}

void BleService::notifyBatteryAlert(int level, int percent) {
    if (!connected_ || !chrBatteryAlert) {
        return;
    }
    const String json = String("{\"level\":") + level + ",\"percent\":" + percent + "}";
    chrBatteryAlert->setValue(json.c_str());
    chrBatteryAlert->notify();
}

void BleService::notifyWatchfaceMeta() {
    if (!connected_ || !chrWatchfaceMeta) {
        return;
    }
    const String json = watchfaceStore.toJson();
    chrWatchfaceMeta->setValue(json.c_str());
    chrWatchfaceMeta->notify();
}

void BleService::notifyAlarmSync() {
    if (!connected_ || !chrAlarmSync) {
        return;
    }
    const uint8_t ping = TODO_PING_CHANGED;
    chrAlarmSync->notify(&ping, 1);
}

bool BleService::consumeTodoAddedNotify() {
    if (!pendingTodoAddedNotify_) {
        return false;
    }
    pendingTodoAddedNotify_ = false;
    return true;
}
