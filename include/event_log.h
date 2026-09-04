// SPDX-License-Identifier: GPL-3.0-or-later
// Event log.
//
// - RAM ring of EVENT_LOG_SIZE entries; oldest drops when full.
// - Every entry is prefixed with a wall-clock timestamp when NTP has synced,
//   or a `+HH:MM:SS` uptime marker otherwise.
// - The last EVENT_LOG_PERSIST_SIZE entries are mirrored to NVS on every write
//   so an ESP reboot (auto-recovery, crash, watchdog) does NOT wipe the last
//   dozen events — the ones you most want to diagnose the reboot with.
#pragma once

#include <Arduino.h>

constexpr uint8_t EVENT_LOG_SIZE         = 60;  // RAM ring
constexpr uint8_t EVENT_LOG_PERSIST_SIZE = 24;  // NVS mirror (subset of RAM)

// Restore the persistent tail from NVS into the RAM ring. Called once early
// in setup() before any logEvent() calls so pre-reboot events appear first.
void   loadPersistentEventLog();

void   logEvent(const String &message);
String getEventLogText();
void   clearEventLog();
