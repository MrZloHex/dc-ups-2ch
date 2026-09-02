// SPDX-License-Identifier: MIT
// DC-UPS-2CH — журнал событий в оперативной памяти.
#pragma once

#include <Arduino.h>

constexpr uint8_t EVENT_LOG_SIZE = 60;

void   logEvent(const String &message);
String getEventLogText();
void   clearEventLog();
