// SPDX-License-Identifier: GPL-3.0-or-later
// Emergency deep sleep (timer wake) + Shelf Sleep (button-only wake).
#pragma once

#include <Arduino.h>

// RTC memory survives deep sleep and software reset.
extern RTC_DATA_ATTR bool     rtcEmergencySleep;
extern RTC_DATA_ATTR bool     rtcShelfSleep;
extern RTC_DATA_ATTR uint32_t rtcSleepWakeCount;

extern unsigned long sleepLowSince;

[[noreturn]] void emergencySleepTimerOnly();
[[noreturn]] void enterEmergencySleep(const String &reason);
[[noreturn]] void enterShelfSleep();

void deepSleepTick();
