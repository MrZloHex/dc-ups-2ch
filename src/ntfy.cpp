// SPDX-License-Identifier: GPL-3.0-or-later
#include "ntfy.h"

#include "ups_common.h"
#include "event_log.h"
#include "power.h"
#include "recovery.h"
#include "config_store.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

bool          ntfyIpPending       = false;
String        ntfyIpReason;
unsigned long ntfyNextTry         = 0;
unsigned long lastNtfyCommandPoll = 0;
String        ntfyLastSeenId;
String        ntfyLastCommandId;

bool sendNtfy(const String &text)
{
    if (WiFi.status() != WL_CONNECTED || cfg.ntfy == "")
        return false;

    WiFiClientSecure client;
    HTTPClient       http;

    client.setInsecure();
    client.setTimeout(5000);

    String url = "https://ntfy.sh/" + cfg.ntfy;

    if (!http.begin(client, url))
        return false;

    http.setTimeout(5000);
    http.addHeader("Title", "Дача Роутер ИБП");
    http.addHeader("Content-Type", "text/plain; charset=utf-8");

    int code = http.POST(text);

    if (code < 200 || code >= 300)
    {
        Serial.printf("[ntfy] -> %d", code);
        if (code > 0)
            Serial.printf(": %s", http.getString().c_str());
        Serial.println();
    }

    http.end();
    return code >= 200 && code < 300;
}

String buildStatusMessage(const String &headline)
{
    String inet;
    if (!internetKnown)      inet = "не проверен";
    else                     inet = internetReachable ? "есть" : "нет/не отвечает";

    String msg = headline;
    msg += "\nАКБ: "     + String(lastVbatt, 2) + " В";
    msg += "\n24В: "     + String(lastVgrid, 2) + " В";
    msg += "\nСеть: "    + String(gridPresent ? "есть" : "нет");
    msg += "\nROUTER: "  + String(routerOn ? "ON" : "OFF") + " (" + modeText(routerMode) + ")";
    msg += "\nONT: "     + String(ontOn    ? "ON" : "OFF") + " (" + modeText(ontMode)    + ")";
    msg += "\nInternet: " + inet;
    msg += "\nRecovery: " + recoveryStatus;
    msg += "\nUptime: "  + formatDuration(millis());
    msg += "\nОтключений сети: " + String(outageCount);

    if (WiFi.status() == WL_CONNECTED)
    {
        msg += "\nWiFi RSSI: " + String(WiFi.RSSI()) + " dBm";
        msg += "\nIP: "        + panelUrl();
        msg += "\nmDNS: "      + mdnsUrl();
    }
    else
        msg += "\nWiFi: не подключён";

    return msg;
}

// Minimal JSON string-field parser for ntfy responses. Only the short
// id/event/message fields are needed — no external library required.
static bool jsonStringField(const String &json, const String &key, String &out)
{
    String marker = "\"" + key + "\":";
    int p = json.indexOf(marker);
    if (p < 0) return false;
    p += marker.length();
    while (p < (int)json.length() && (json[p] == ' ' || json[p] == '\t')) p++;
    if (p >= (int)json.length() || json[p] != '\"') return false;
    p++;

    out = "";
    bool esc = false;
    for (; p < (int)json.length(); ++p)
    {
        char c = json[p];
        if (esc)
        {
            if      (c == 'n') out += '\n';
            else if (c == 'r') out += '\r';
            else if (c == 't') out += '\t';
            else               out += c;
            esc = false;
        }
        else if (c == '\\')     esc = true;
        else if (c == '\"')     return true;
        else                    out += c;
    }
    return false;
}

static void handleNtfyCommand(const String &id, String command)
{
    command.trim();
    command.toLowerCase();

    bool isStatus = (command == "!ups status" || command == "!ups");
    bool isPing   = (command == "!ups ping");
    if (!isStatus && !isPing) return;

    // Persist the command ID (not every notification's ID) so the same
    // command isn't re-executed after a reboot.
    saveNtfyCommandId(id);
    logEvent("ntfy command: " + command);

    String head = isPing ? "PONG — DC-UPS на связи" : "Статус DC-UPS по запросу ntfy";
    sendNtfy(buildStatusMessage(head));
}

void queueIpNotification(const String &reason)
{
    if (cfg.ntfy == "")
        return;

    ntfyIpReason  = reason;
    ntfyIpPending = true;
    ntfyNextTry   = millis() + 1200;
}

void ntfyTick()
{
    if (!ntfyIpPending)               return;
    if (WiFi.status() != WL_CONNECTED) return;
    if ((long)(millis() - ntfyNextTry) < 0) return;

    String msg = ntfyIpReason +
                 "\nIP: "   + panelUrl() +
                 "\nmDNS: " + mdnsUrl() +
                 "\nАКБ: "  + String(lastVbatt, 1) + " В" +
                 "\nСеть: " + String(gridPresent ? "есть" : "нет");

    if (sendNtfy(msg))
    {
        ntfyIpPending = false;
        Serial.println("[ntfy] IP sent: " + panelUrl());
    }
    else
    {
        ntfyNextTry = millis() + NTFY_RETRY_MS;
    }
}

void ntfyCommandTick()
{
    if (cfg.ntfy == "" || WiFi.status() != WL_CONNECTED) return;
    if (millis() - lastNtfyCommandPoll < NTFY_COMMAND_POLL_MS) return;
    lastNtfyCommandPoll = millis();

    WiFiClientSecure client;
    HTTPClient       http;
    client.setInsecure();
    client.setTimeout(5000);

    String since = ntfyLastSeenId == "" ? "latest" : ntfyLastSeenId;
    String url   = "https://ntfy.sh/" + cfg.ntfy + "/json?poll=1&since=" + since;
    if (!http.begin(client, url)) return;

    http.setTimeout(5000);
    int code = http.GET();
    if (code < 200 || code >= 300)
    {
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    int start = 0;
    while (start < (int)body.length())
    {
        int end = body.indexOf('\n', start);
        if (end < 0) end = body.length();
        String line = body.substring(start, end);
        line.trim();
        start = end + 1;
        if (line == "") continue;

        String id, event, message;
        if (!jsonStringField(line, "id",    id))    continue;
        if (!jsonStringField(line, "event", event)) continue;

        // Server may include the cursor message itself; don't re-execute it.
        if (id == ntfyLastSeenId) continue;

        // Always advance the RAM cursor, including the device's own replies.
        ntfyLastSeenId = id;
        if (event != "message") continue;
        if (!jsonStringField(line, "message", message)) continue;

        handleNtfyCommand(id, message);
    }
}
