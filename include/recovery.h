// SPDX-License-Identifier: GPL-3.0-or-later
// Internet health probing + staged auto-recovery (power-cycle → ESP reboot).
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
