// SPDX-License-Identifier: GPL-3.0-or-later
// RAM ring-buffer event log.
#pragma once

#include <Arduino.h>

constexpr uint8_t EVENT_LOG_SIZE = 60;

void   logEvent(const String &message);
String getEventLogText();
void   clearEventLog();
