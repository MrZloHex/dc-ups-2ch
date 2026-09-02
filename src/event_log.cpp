// SPDX-License-Identifier: GPL-3.0-or-later
#include "event_log.h"
#include "ups_common.h"

static String  s_log[EVENT_LOG_SIZE];
static uint8_t s_head  = 0;
static uint8_t s_count = 0;

void logEvent(const String &message)
{
    String line = "[" + formatDuration(millis()) + "] " + message;
    s_log[s_head] = line;
    s_head = (s_head + 1) % EVENT_LOG_SIZE;
    if (s_count < EVENT_LOG_SIZE)
        s_count++;

    Serial.println(line);
}

String getEventLogText()
{
    String out;
    out.reserve(5000);

    uint8_t first = (s_head + EVENT_LOG_SIZE - s_count) % EVENT_LOG_SIZE;
    for (uint8_t i = 0; i < s_count; ++i)
    {
        uint8_t idx = (first + i) % EVENT_LOG_SIZE;
        out += s_log[idx];
        out += "\n";
    }

    if (s_count == 0)
        out = "Журнал пуст.\n";

    return out;
}

void clearEventLog()
{
    for (uint8_t i = 0; i < EVENT_LOG_SIZE; ++i)
        s_log[i] = "";
    s_head  = 0;
    s_count = 0;
    logEvent("Журнал очищен");
}
