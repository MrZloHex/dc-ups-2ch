// SPDX-License-Identifier: GPL-3.0-or-later
// NTP wall-clock sync. Kicked off on every successful WiFi connect; the
// SNTP subsystem runs asynchronously in the ESP-IDF background so callers
// don't wait. `wallClockNow()` returns a formatted string once the clock
// has been set, or an empty string until then.
#pragma once

#include <Arduino.h>

// Compile-time timezone offset in seconds East of UTC. MSK = UTC+3, no DST.
// Change this and rebuild to relocate the device to a different zone.
constexpr long TZ_OFFSET_SEC = 3L * 3600L;

// Idempotent — safe to call on every WiFi reconnect.
void   ntpBegin();

// Non-blocking: returns true only if the SNTP subsystem has already
// installed a real wall-clock time.
bool   timeSynced();

// "YYYY-MM-DD HH:MM:SS" (local, per TZ_OFFSET_SEC) if synced; "" otherwise.
String wallClockNow();

// Compact "HH:MM:SS" form for inline log entries.
String wallClockShort();
