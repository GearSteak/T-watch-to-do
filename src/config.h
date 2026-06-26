#pragma once

#define FIRMWARE_VERSION "0.1.15"
#define DEVICE_NAME "TWatch-Companion"

#define SVC_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHR_DEVICE_INFO "6e400010-b5a3-f393-e0a9-e50e24dcca9e"
#define CHR_TIME_SYNC "6e400011-b5a3-f393-e0a9-e50e24dcca9e"
#define CHR_TODO_SYNC "6e400012-b5a3-f393-e0a9-e50e24dcca9e"
#define CHR_BATTERY_ALERT "6e400013-b5a3-f393-e0a9-e50e24dcca9e"
#define CHR_COMPLETED_LOG "6e400014-b5a3-f393-e0a9-e50e24dcca9e"
#define CHR_WATCHFACE_META "6e400015-b5a3-f393-e0a9-e50e24dcca9e"
#define CHR_WATCHFACE_IMAGE "6e400016-b5a3-f393-e0a9-e50e24dcca9e"
#define CHR_ALARM_SYNC "6e400017-b5a3-f393-e0a9-e50e24dcca9e"

#define TODO_FILE "/todos.json"
#define WATCHFACE_FILE "/watchface.json"
#define WATCHFACE_IMAGE_FILE "/watchface.bin"
#define ALARM_FILE "/alarms.json"

#define WATCHFACE_IMAGE_W 240
#define WATCHFACE_IMAGE_H 240
#define WATCHFACE_IMAGE_BYTES (WATCHFACE_IMAGE_W * WATCHFACE_IMAGE_H * 2)

// Image upload protocol opcodes (first byte of each write to CHR_WATCHFACE_IMAGE)
#define WF_IMG_OP_BEGIN 0x01
#define WF_IMG_OP_DATA 0x02
#define WF_IMG_OP_COMMIT 0x03
#define WF_IMG_OP_CLEAR 0x04

// Todo transfer protocol (CHR_TODO_SYNC). A single GATT value is capped at 512
// bytes, so the list is transferred in pages instead of one big value.
// Web -> watch (writes):
#define TODO_OP_UPLOAD_BEGIN 0x10   // [0x10][u32 totalLen]
#define TODO_OP_UPLOAD_DATA 0x11    // [0x11][u32 offset][bytes]
#define TODO_OP_UPLOAD_COMMIT 0x12  // [0x12]
#define TODO_OP_DL_PREPARE 0x20     // [0x20] -> value becomes [u32 totalLen]
#define TODO_OP_DL_PAGE 0x21        // [0x21][u32 offset] -> value becomes page bytes
// Watch -> web (notify): single-byte ping meaning "list changed, re-download".
#define TODO_PING_CHANGED 0x40

#define TODO_PAGE_SIZE 400
#define TODO_UPLOAD_MAX 16384
#define MAX_TODOS 50
#define MAX_TODO_TEXT 120

#define TODO_REPEAT_NONE 0
#define TODO_REPEAT_DAILY 1
#define TODO_REPEAT_WEEKLY 2
#define TODO_REPEAT_INTERVAL 3

#define MAX_ALARMS 8
#define ALARM_LABEL_MAX 40
#define ALARM_UPLOAD_MAX 4096

#define BATTERY_POLL_MS_NORMAL 60000
#define BATTERY_POLL_MS_LOW 30000
#define BATTERY_LOW_THRESHOLD 25

// Turn the watch face off after this many ms without touch. Tap to wake.
#define DISPLAY_SLEEP_TIMEOUT_MS 30000
#define DISPLAY_ACTIVE_BRIGHTNESS 180
