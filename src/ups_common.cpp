// SPDX-License-Identifier: GPL-3.0-or-later
// Shared globals and utility functions.
#include "ups_common.h"

#include <WiFi.h>
#include "esp_task_wdt.h"

Config      cfg;
Preferences prefs;

String formatDuration(uint32_t ms)
{
    uint32_t total   = ms / 1000UL;
    uint32_t days    = total / 86400UL; total %= 86400UL;
    uint32_t hours   = total / 3600UL;  total %= 3600UL;
    uint32_t minutes = total / 60UL;
    uint32_t seconds = total % 60UL;

    char buf[40];
    if (days > 0)
        snprintf(buf, sizeof(buf), "%lu д %02lu:%02lu:%02lu",
                 (unsigned long)days, (unsigned long)hours,
                 (unsigned long)minutes, (unsigned long)seconds);
    else
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                 (unsigned long)hours, (unsigned long)minutes,
                 (unsigned long)seconds);
    return String(buf);
}

String htmlEscape(String s)
{
    s.replace("&", "&amp;");
    s.replace("\"", "&quot;");
    s.replace("'", "&#39;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    return s;
}

String jsonEscape(String s)
{
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    s.replace("\r", "\\r");
    s.replace("\n", "\\n");
    return s;
}

void waitWithWatchdog(uint32_t ms)
{
    unsigned long started = millis();
    while (millis() - started < ms)
    {
        esp_task_wdt_reset();
        delay(100);
    }
}

float readVoltage(int pin, float div, float cal)
{
    uint32_t acc = 0;
    for (uint8_t i = 0; i < 32; ++i)
    {
        acc += analogReadMilliVolts(pin);
        delay(2);
    }
    return ((acc / 32.0f) / 1000.0f) * div * cal;
}

String panelUrl()
{
    if (WiFi.status() != WL_CONNECTED)
        return "";
    return "http://" + WiFi.localIP().toString() + "/";
}

String mdnsUrl()
{
    return String("http://") + MDNS_HOST + ".local/";
}

String modeText(LoadMode m)
{
    if (m == LM_ON)  return "ручн.ВКЛ";
    if (m == LM_OFF) return "ручн.ВЫКЛ";
    return "авто";
}

String targetText(PowerTarget t)
{
    if (t == TARGET_ROUTER) return "ROUTER";
    if (t == TARGET_ONT)    return "ONT";
    if (t == TARGET_BOTH)   return "ROUTER+ONT";
    return "—";
}

bool targetIncludesRouter(PowerTarget t) { return t == TARGET_ROUTER || t == TARGET_BOTH; }
bool targetIncludesOnt   (PowerTarget t) { return t == TARGET_ONT    || t == TARGET_BOTH; }
