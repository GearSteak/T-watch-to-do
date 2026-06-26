#include "time_service.h"

#include <LilyGoLib.h>
#include <sys/time.h>
#include <time.h>

bool TimeService::setFromUnixMs(int64_t unixMs, int32_t tzOffsetMinutes) {
    struct timeval tv;
    tv.tv_sec = unixMs / 1000;
    tv.tv_usec = (unixMs % 1000) * 1000;
    settimeofday(&tv, nullptr);

    char tzBuf[16];
    const int hours = tzOffsetMinutes / 60;
    const int mins = abs(tzOffsetMinutes % 60);
    snprintf(tzBuf, sizeof(tzBuf), "UTC%+d:%02d", -hours, mins);
    setenv("TZ", tzBuf, 1);
    tzset();

    instance.rtc.hwClockWrite();
    return true;
}

String TimeService::formatTime(const char *fmt) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--:--";
    }
    char buf[32];
    strftime(buf, sizeof(buf), fmt, &timeinfo);
    return String(buf);
}

String TimeService::formatDateTime(const char *fmt) {
    return formatTime(fmt);
}
