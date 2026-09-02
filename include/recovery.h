// SPDX-License-Identifier: MIT
// DC-UPS-2CH — автоматическое восстановление связи (probe + power-cycle).
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

extern RTC_DATA_ATTR uint8_t rtcAutoPowerCycles;
extern RTC_DATA_ATTR uint8_t rtcNetReboots;

extern unsigned long lastInternetCheck;
extern unsigned long networkBadSince;
extern unsigned long internetHealthySince;
extern bool          internetKnown;
extern bool          internetReachable;
extern String        recoveryStatus;

bool tcpProbe(IPAddress ip, uint16_t port);
bool checkInternetReachable();

void networkRecoveryTick();
