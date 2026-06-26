#pragma once

#include <Arduino.h>
#include <functional>
#include <string>

class BleService {
public:
    using TodoChangeCallback = std::function<void()>;

    bool begin();
    void notifyDeviceInfo();
    void notifyTodoSync();
    void notifyCompletedLog();
    void notifyBatteryAlert(int level, int percent);
    void notifyWatchfaceMeta();
    void notifyAlarmSync();

    void loopTick();

    // Refreshes the stored characteristic values (todos, completed log,
    // watchface, device info) so GATT reads always return current data.
    void updateStoredValues();

    void queueTodoWrite(const std::string &json);
    void queueWatchfaceWrite(const std::string &json);
    void queueAlarmWrite(const std::string &json);

    // Paged todo transfer (called from the CHR_TODO_SYNC write callback).
    void todoDownloadPrepare();
    void todoDownloadPage(uint32_t offset);
    void todoUploadBegin(uint32_t totalSize);
    void todoUploadData(uint32_t offset, const uint8_t *data, size_t len);
    void todoUploadCommit();

    // Paged alarm transfer (CHR_ALARM_SYNC).
    void alarmDownloadPrepare();
    void alarmDownloadPage(uint32_t offset);
    void alarmUploadBegin(uint32_t totalSize);
    void alarmUploadData(uint32_t offset, const uint8_t *data, size_t len);
    void alarmUploadCommit();

    // Image upload (chunked) handlers, called from the BLE write callback.
    void imageBegin(uint32_t totalSize);
    void imageData(uint32_t offset, const uint8_t *data, size_t len);
    void imageCommit();
    void imageClear();

    using WatchfaceChangeCallback = std::function<void()>;

    void setTodoChangeCallback(TodoChangeCallback cb) { todoChangeCb_ = std::move(cb); }
    void setWatchfaceChangeCallback(WatchfaceChangeCallback cb) { watchfaceChangeCb_ = std::move(cb); }
    void setAlarmChangeCallback(TodoChangeCallback cb) { alarmChangeCb_ = std::move(cb); }
    void handleTodoChanged() {
        if (todoChangeCb_) {
            todoChangeCb_();
        }
    }

    bool isConnected() const { return connected_; }
    void setConnected(bool connected) { connected_ = connected; }
    bool consumeTodoAddedNotify();

private:
    bool connected_ = false;
    TodoChangeCallback todoChangeCb_;
    WatchfaceChangeCallback watchfaceChangeCb_;
    TodoChangeCallback alarmChangeCb_;
    volatile bool pendingTodoWrite_ = false;
    volatile bool pendingWatchfaceWrite_ = false;
    volatile bool pendingAlarmWrite_ = false;
    volatile bool pendingTodoAddedNotify_ = false;
    std::string pendingTodoJson_;
    std::string pendingWatchfaceJson_;
    std::string pendingAlarmJson_;

    uint8_t *imageBuf_ = nullptr;
    uint32_t imageExpected_ = 0;
    uint32_t imageReceived_ = 0;
    volatile bool pendingImageCommit_ = false;
    volatile bool pendingImageClear_ = false;

    std::string todoDownloadBuf_;
    uint8_t *todoUpBuf_ = nullptr;
    uint32_t todoUpExpected_ = 0;

    std::string alarmDownloadBuf_;
    uint8_t *alarmUpBuf_ = nullptr;
    uint32_t alarmUpExpected_ = 0;
};

extern BleService bleService;
