#include "app_ui.h"
#include "config.h"
#include "storage/todo_store.h"
#include "storage/watchface_store.h"
#include "services/battery_monitor.h"
#include "services/time_service.h"
#include "services/device_reset.h"
#include "services/display_sleep.h"
#include <LilyGoLib.h>
#include <FFat.h>
#include <esp_heap_caps.h>
#include <vector>

static lv_obj_t *statusBar = nullptr;
static lv_obj_t *statusTime = nullptr;
static lv_obj_t *statusBattery = nullptr;
static lv_obj_t *tabView = nullptr;
static lv_obj_t *todoList = nullptr;
static lv_obj_t *watchfaceCont = nullptr;
static lv_obj_t *watchfaceTime = nullptr;
static lv_obj_t *watchfaceDate = nullptr;
static lv_obj_t *watchfaceBattery = nullptr;
static lv_obj_t *alertOverlay = nullptr;
static lv_obj_t *alarmOverlay = nullptr;
static lv_timer_t *clockTimer = nullptr;
static std::vector<String> todoIds;

static uint8_t *faceImgBuf = nullptr;
static lv_image_dsc_t faceImgDsc;
static bool faceImgLoaded = false;

static bool loadWatchfaceImage() {
    faceImgLoaded = false;
    if (!FFat.exists(WATCHFACE_IMAGE_FILE)) {
        return false;
    }
    File f = FFat.open(WATCHFACE_IMAGE_FILE, "r");
    if (!f) {
        return false;
    }
    const size_t size = f.size();
    if (size < WATCHFACE_IMAGE_BYTES) {
        f.close();
        return false;
    }
    if (!faceImgBuf) {
        faceImgBuf = (uint8_t *)heap_caps_malloc(WATCHFACE_IMAGE_BYTES, MALLOC_CAP_SPIRAM);
        if (!faceImgBuf) {
            faceImgBuf = (uint8_t *)malloc(WATCHFACE_IMAGE_BYTES);
        }
    }
    if (!faceImgBuf) {
        f.close();
        return false;
    }
    const size_t read = f.read(faceImgBuf, WATCHFACE_IMAGE_BYTES);
    f.close();
    if (read < WATCHFACE_IMAGE_BYTES) {
        return false;
    }

    memset(&faceImgDsc, 0, sizeof(faceImgDsc));
    faceImgDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    faceImgDsc.header.cf = LV_COLOR_FORMAT_RGB565;
    faceImgDsc.header.w = WATCHFACE_IMAGE_W;
    faceImgDsc.header.h = WATCHFACE_IMAGE_H;
    faceImgDsc.header.stride = WATCHFACE_IMAGE_W * 2;
    faceImgDsc.data = faceImgBuf;
    faceImgDsc.data_size = WATCHFACE_IMAGE_BYTES;
    faceImgLoaded = true;
    return true;
}

static uint32_t priorityColor(uint8_t priority) {
    switch (priority) {
    case 3: return 0xFF6666; // high - red
    case 2: return 0xFFAA00; // medium - amber
    case 1: return 0x66AAFF; // low - blue
    default: return 0xFFFFFF; // none - normal white text
    }
}


static void vibrateShort() {
    instance.drv.setWaveform(0, 47);
    instance.drv.run();
}

static void updateBatteryLabel(lv_obj_t *label, bool useThemeColor) {
    const auto &bat = batteryMonitor.state();
    lv_label_set_text_fmt(label, "%d%%%s", bat.percent, bat.charging ? "+" : "");
    if (bat.percent <= 10) {
        lv_obj_set_style_text_color(label, lv_color_hex(0xFF4444), 0);
    } else if (bat.percent <= 20) {
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFAA00), 0);
    } else if (useThemeColor) {
        lv_obj_set_style_text_color(label, lv_color_hex(watchfaceStore.config().dateColor), 0);
    } else {
        lv_obj_set_style_text_color(label, lv_color_hex(0x88FF88), 0);
    }
}

static void refreshClock() {
    if (watchfaceTime) {
        lv_label_set_text(watchfaceTime, TimeService::formatTime("%H:%M").c_str());
    }
    if (watchfaceDate) {
        lv_label_set_text(watchfaceDate, TimeService::formatDateTime("%a %b %d").c_str());
    }
    if (statusTime) {
        lv_label_set_text(statusTime, TimeService::formatTime("%H:%M").c_str());
    }
    if (statusBattery) {
        updateBatteryLabel(statusBattery, false);
    }
    if (watchfaceBattery) {
        updateBatteryLabel(watchfaceBattery, true);
    }
}

static void clockTimerCb(lv_timer_t *) {
    refreshClock();
}

static void todoCheckboxEvent(lv_event_t *e) {
    const char *id = (const char *)lv_event_get_user_data(e);
    todoStore.toggle(id);
    vibrateShort();
    // Do NOT rebuild the list here: lv_obj_clean() would delete this very
    // checkbox while its event is still running (use-after-free). toggle()
    // already marks the store dirty, so the main loop refreshes safely.
}


static lv_obj_t *createStatusBar(lv_obj_t *parent) {
    statusBar = lv_obj_create(parent);
    lv_obj_set_size(statusBar, LV_PCT(100), 24);
    lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(statusBar, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(statusBar, 0, 0);
    lv_obj_set_style_pad_hor(statusBar, 6, 0);
    lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_SCROLLABLE);

    statusTime = lv_label_create(statusBar);
    lv_label_set_text(statusTime, "--:--");
    lv_obj_align(statusTime, LV_ALIGN_LEFT_MID, 0, 0);

    statusBattery = lv_label_create(statusBar);
    lv_label_set_text(statusBattery, "--%");
    lv_obj_align(statusBattery, LV_ALIGN_RIGHT_MID, 0, 0);

    return statusBar;
}

static void buildWatchfaceWidgets() {
    if (!watchfaceCont) {
        return;
    }

    watchfaceTime = nullptr;
    watchfaceDate = nullptr;
    watchfaceBattery = nullptr;
    lv_obj_clean(watchfaceCont);

    const WatchfaceConfig &cfg = watchfaceStore.config();
    lv_obj_set_style_bg_color(watchfaceCont, lv_color_hex(cfg.bgColor), 0);

    if (cfg.hasImage && loadWatchfaceImage()) {
        lv_obj_t *bg = lv_image_create(watchfaceCont);
        lv_image_set_src(bg, &faceImgDsc);
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_background(bg);
    }

    watchfaceTime = lv_label_create(watchfaceCont);
    lv_label_set_text(watchfaceTime, TimeService::formatTime("%H:%M").c_str());
    lv_obj_set_style_text_color(watchfaceTime, lv_color_hex(cfg.timeColor), 0);
    lv_obj_set_style_text_font(watchfaceTime, &lv_font_montserrat_28, 0);

    if (cfg.preset == "bold") {
        lv_obj_align(watchfaceTime, LV_ALIGN_CENTER, 0, -8);
    } else if (cfg.preset == "minimal") {
        lv_obj_align(watchfaceTime, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_align(watchfaceTime, LV_ALIGN_CENTER, 0, -20);
    }

    if (cfg.showDate && cfg.preset != "minimal") {
        watchfaceDate = lv_label_create(watchfaceCont);
        lv_label_set_text(watchfaceDate, TimeService::formatDateTime("%a %b %d").c_str());
        lv_obj_set_style_text_color(watchfaceDate, lv_color_hex(cfg.dateColor), 0);
        if (cfg.preset == "bold") {
            lv_obj_align(watchfaceDate, LV_ALIGN_CENTER, 0, 28);
        } else {
            lv_obj_align(watchfaceDate, LV_ALIGN_CENTER, 0, 16);
        }
    }

    if (cfg.showBattery && cfg.preset != "minimal") {
        watchfaceBattery = lv_label_create(watchfaceCont);
        lv_label_set_text(watchfaceBattery, "--%");
        lv_obj_align(watchfaceBattery, LV_ALIGN_BOTTOM_MID, 0, -8);
        updateBatteryLabel(watchfaceBattery, true);
    }
}

static void buildWatchfaceTab(lv_obj_t *parent) {
    watchfaceCont = lv_obj_create(parent);
    lv_obj_set_size(watchfaceCont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(watchfaceCont, 0, 0);
    lv_obj_clear_flag(watchfaceCont, LV_OBJ_FLAG_SCROLLABLE);
    buildWatchfaceWidgets();
}

static void buildTodosTab(lv_obj_t *parent) {
    todoList = lv_list_create(parent);
    lv_obj_set_size(todoList, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(todoList, lv_color_hex(0x0f0f1a), 0);
}

static void resetConfirmEvent(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    vibrateShort();
    DeviceReset::restart();
}

static void resetCancelEvent(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (mbox) {
        lv_msgbox_close(mbox);
    }
}

static void showRestartConfirm() {
    lv_obj_t *mbox = lv_msgbox_create(nullptr);
    lv_msgbox_add_title(mbox, "Restart watch?");
    lv_msgbox_add_text(mbox, "Turns the watch off and on.\nYour todos are kept.");
    lv_obj_t *btnRestart = lv_msgbox_add_footer_button(mbox, "Restart");
    lv_obj_t *btnCancel = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_set_style_text_color(btnRestart, lv_color_hex(0x66AAFF), 0);
    lv_obj_add_event_cb(btnRestart, resetConfirmEvent, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnCancel, resetCancelEvent, LV_EVENT_CLICKED, mbox);
    lv_obj_center(mbox);
}

static void resetButtonEvent(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        showRestartConfirm();
    }
}

static void facePresetEvent(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    vibrateShort();
    watchfaceStore.cyclePreset();
}

static void buildSettingsTab(lv_obj_t *parent) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x0f0f1a), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont, 12, 0);
    lv_obj_set_style_pad_row(cont, 10, 0);
    // Ensure all child text is readable on the dark background.
    lv_obj_set_style_text_color(cont, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

    lv_obj_t *version = lv_label_create(cont);
    lv_label_set_text_fmt(version, "Firmware %s", FIRMWARE_VERSION);
    lv_obj_set_style_text_color(version, lv_color_hex(0xAAAAAA), 0);

    lv_obj_t *todoCount = lv_label_create(cont);
    lv_label_set_text_fmt(todoCount, "Todos: %u", (unsigned)todoStore.items().size());
    lv_obj_set_style_text_color(todoCount, lv_color_hex(0xAAAAAA), 0);

    lv_obj_t *faceLabel = lv_label_create(cont);
    lv_label_set_text(faceLabel, "Watch face");

    lv_obj_t *btnFace = lv_btn_create(cont);
    lv_obj_set_width(btnFace, LV_PCT(90));
    lv_obj_t *lblFace = lv_label_create(btnFace);
    lv_label_set_text(lblFace, "Change face style");
    lv_obj_set_style_text_color(lblFace, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblFace);
    lv_obj_add_event_cb(btnFace, facePresetEvent, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *faceHint = lv_label_create(cont);
    lv_label_set_text_fmt(faceHint, "Current: %s", watchfaceStore.config().preset.c_str());
    lv_obj_set_style_text_color(faceHint, lv_color_hex(0x888888), 0);

    lv_obj_t *btnReset = lv_btn_create(cont);
    lv_obj_set_width(btnReset, LV_PCT(90));
    lv_obj_set_style_bg_color(btnReset, lv_color_hex(0x223366), 0);
    lv_obj_t *lblReset = lv_label_create(btnReset);
    lv_label_set_text(lblReset, "Restart watch");
    lv_obj_set_style_text_color(lblReset, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblReset);
    lv_obj_add_event_cb(btnReset, resetButtonEvent, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *hint = lv_label_create(cont);
    lv_label_set_text(hint, "Power off and on — keeps your data");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, LV_PCT(90));
}

void AppUi::refreshTodos() {
    if (!todoList) {
        return;
    }
    lv_obj_clean(todoList);
    todoIds.clear();
    // Reserve up front so the vector never reallocates while we hand out
    // c_str() pointers as checkbox user-data (reallocation would dangle them).
    todoIds.reserve(todoStore.items().size());

    for (const TodoItem &item : todoStore.items()) {
        lv_obj_t *row = lv_list_add_btn(todoList, nullptr, "");
        lv_obj_set_height(row, 36);
        lv_obj_set_style_pad_all(row, 4, 0);
        // The default LVGL theme gives list buttons a light background, which
        // hides the white (no-priority) text. Force a dark row so every
        // priority colour — including plain white — is readable.
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2a2a44), LV_STATE_PRESSED);
        // List buttons use an automatic flex layout that pushes manually placed
        // children off-screen. Disable it so the checkbox stays pinned right.
        lv_obj_set_layout(row, LV_LAYOUT_NONE);

        // The watch is for viewing and checking off. Priority (text color) and
        // ordering are managed from the web app, so no controls clutter the row.
        lv_obj_t *text = lv_label_create(row);
        String displayText = item.text;
        if (item.repeat != TODO_REPEAT_NONE) {
            displayText = String(LV_SYMBOL_REFRESH) + " " + item.text;
        }
        lv_label_set_text(text, displayText.c_str());
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(text, lv_color_hex(priorityColor(item.priority)), 0);
        lv_obj_set_width(text, 168);
        lv_obj_align(text, LV_ALIGN_LEFT_MID, 4, 0);

        lv_obj_t *cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, "");
        if (item.done) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        lv_obj_align(cb, LV_ALIGN_RIGHT_MID, -2, 0);

        todoIds.push_back(item.id);
        lv_obj_add_event_cb(cb, todoCheckboxEvent, LV_EVENT_VALUE_CHANGED, (void *)todoIds.back().c_str());
    }
}

void AppUi::refreshWatchface() {
    buildWatchfaceWidgets();
    refreshClock();
}

void AppUi::refreshStatusBar() {
    refreshClock();
}

void AppUi::showBatteryAlert(int level, int percent) {
    displaySleep.wake();

    if (alertOverlay) {
        lv_obj_del(alertOverlay);
        alertOverlay = nullptr;
    }

    vibrateShort();
    vibrateShort();

    alertOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(alertOverlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(alertOverlay, lv_color_hex(0x220000), 0);
    lv_obj_set_style_bg_opa(alertOverlay, LV_OPA_90, 0);
    lv_obj_clear_flag(alertOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(alertOverlay);
    lv_label_set_text_fmt(title, "Battery Low\n%d%%", percent);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF6666), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *sub = lv_label_create(alertOverlay);
    lv_label_set_text_fmt(sub, "Threshold: %d%%\nTap to dismiss", level);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 30);

    lv_obj_add_event_cb(alertOverlay, [](lv_event_t *e) {
        lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            lv_obj_del(obj);
            alertOverlay = nullptr;
        }
    }, LV_EVENT_CLICKED, nullptr);
}

void AppUi::showReminderAlert(const char *text) {
    showAlarmAlert(text, "Reminder");
}

void AppUi::showAlarmAlert(const char *label, const char *titleText) {
    displaySleep.wake();

    if (alarmOverlay) {
        lv_obj_del(alarmOverlay);
        alarmOverlay = nullptr;
    }

    vibrateShort();
    vibrateShort();
    vibrateShort();

    alarmOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(alarmOverlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(alarmOverlay, lv_color_hex(0x001133), 0);
    lv_obj_set_style_bg_opa(alarmOverlay, LV_OPA_90, 0);
    lv_obj_clear_flag(alarmOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(alarmOverlay);
    lv_label_set_text(title, titleText);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x66AAFF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -36);

    lv_obj_t *name = lv_label_create(alarmOverlay);
    lv_label_set_text(name, label);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(name, LV_PCT(90));
    lv_obj_align(name, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *hint = lv_label_create(alarmOverlay);
    lv_label_set_text(hint, "Tap to dismiss");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 44);

    lv_obj_add_event_cb(alarmOverlay, [](lv_event_t *e) {
        lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            lv_obj_del(obj);
            alarmOverlay = nullptr;
        }
    }, LV_EVENT_CLICKED, nullptr);
}

void AppUi::notifyTodoAdded() {
    displaySleep.wake();
    vibrateShort();
    vibrateShort();
}

void AppUi::vibrateShort() {
    vibrateShort();
}

bool AppUi::isAlertVisible() {
    return alertOverlay != nullptr || alarmOverlay != nullptr;
}

void AppUi::init() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f0f1a), 0);

    createStatusBar(scr);

    tabView = lv_tabview_create(scr);
    lv_obj_set_size(tabView, LV_PCT(100), LV_PCT(100) - 24);
    lv_obj_align(tabView, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(tabView, lv_color_hex(0x0f0f1a), 0);

    // Smaller tab bar so the Home/Todos/Settings buttons take less space and sit higher.
    lv_tabview_set_tab_bar_position(tabView, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabView, 30);

    lv_obj_t *tabFace = lv_tabview_add_tab(tabView, LV_SYMBOL_HOME);
    lv_obj_t *tabTodos = lv_tabview_add_tab(tabView, LV_SYMBOL_LIST);
    lv_obj_t *tabSettings = lv_tabview_add_tab(tabView, LV_SYMBOL_SETTINGS);

    lv_obj_t *tabBtns = lv_tabview_get_tab_bar(tabView);
    if (tabBtns) {
        lv_obj_set_style_pad_top(tabBtns, 2, 0);
        lv_obj_set_style_pad_bottom(tabBtns, 2, 0);
        lv_obj_set_style_text_font(tabBtns, &lv_font_montserrat_14, 0);
    }

    buildWatchfaceTab(tabFace);
    buildTodosTab(tabTodos);
    buildSettingsTab(tabSettings);

    refreshTodos();
    refreshClock();
    clockTimer = lv_timer_create(clockTimerCb, 1000, nullptr);
}
