#include "notification.h"

#include <LilyGoLib.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const int CHIME_SAMPLE_RATE = 44100;
static const int CHIME_MS = 90;
static const int CHIME_SAMPLES = (CHIME_SAMPLE_RATE * CHIME_MS) / 1000;
static int16_t chimeBuf[CHIME_SAMPLES];
static bool chimeReady = false;

void Notify::begin() {
    for (int i = 0; i < CHIME_SAMPLES; ++i) {
        const float t = (float)i / (float)CHIME_SAMPLE_RATE;
        const float env = 1.0f - ((float)i / (float)CHIME_SAMPLES);
        chimeBuf[i] = (int16_t)(10000.0f * env * sinf(2.0f * (float)M_PI * 880.0f * t));
    }
    chimeReady = true;
}

void Notify::todoAdded() {
    if (!chimeReady) {
        begin();
    }
    instance.powerControl(POWER_SPEAK, true);
    instance.player.write((uint8_t *)chimeBuf, sizeof(chimeBuf));
}
