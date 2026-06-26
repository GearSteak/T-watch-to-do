#pragma once

#include <Arduino.h>

namespace TimeService {
    bool setFromUnixMs(int64_t unixMs, int32_t tzOffsetMinutes);
    String formatTime(const char *fmt = "%H:%M");
    String formatDateTime(const char *fmt = "%a %b %d %H:%M");
}
