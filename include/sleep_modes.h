// SPDX-License-Identifier: MIT
// DC-UPS-2CH — аварийный deep sleep (по таймеру) и Shelf Sleep (только кнопка).
#pragma once

#include <Arduino.h>

// RTC memory сохраняется через deep sleep и software reset.
extern RTC_DATA_ATTR bool     rtcEmergencySleep;
extern RTC_DATA_ATTR bool     rtcShelfSleep;
extern RTC_DATA_ATTR uint32_t rtcSleepWakeCount;

extern unsigned long sleepLowSince;

[[noreturn]] void emergencySleepTimerOnly();
[[noreturn]] void enterEmergencySleep(const String &reason);
[[noreturn]] void enterShelfSleep();

void deepSleepTick();
