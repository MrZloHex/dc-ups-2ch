// SPDX-License-Identifier: GPL-3.0-or-later
#include "event_log.h"
#include "ups_common.h"
#include "time_sync.h"

#include <Preferences.h>

static String  s_log[EVENT_LOG_SIZE];
static uint8_t s_head  = 0;
static uint8_t s_count = 0;

// NVS namespace is shared with config_store — we own key "elog" inside it.
// Storing the whole tail as a single blob keeps the wear pattern predictable
// and lets us survive on the default NVS partition size.
static constexpr const char *NVS_NS  = "ups";
static constexpr const char *NVS_KEY = "elog";

// Format a uniform timestamp prefix. Wall-clock when NTP is up, uptime with a
// `+` marker otherwise — the marker makes it obvious in-log which entries
// pre-dated the sync.
static String logTimestamp()
{
    String wc = wallClockShort();
    if (wc.length()) return wc;
    return "+" + formatDuration(millis());
}

static void appendToRing(const String &line)
{
    s_log[s_head] = line;
    s_head = (s_head + 1) % EVENT_LOG_SIZE;
    if (s_count < EVENT_LOG_SIZE)
        s_count++;
}

// Serialize the last EVENT_LOG_PERSIST_SIZE entries as `\n`-joined text. Kept
// well below NVS's 4000-byte per-string limit even if every line is long.
static String buildPersistBlob()
{
    uint8_t persistCount = s_count < EVENT_LOG_PERSIST_SIZE
                             ? s_count
                             : EVENT_LOG_PERSIST_SIZE;
    uint8_t start = (s_head + EVENT_LOG_SIZE - persistCount) % EVENT_LOG_SIZE;

    String blob;
    blob.reserve(persistCount * 96);
    for (uint8_t i = 0; i < persistCount; ++i)
    {
        uint8_t idx = (start + i) % EVENT_LOG_SIZE;
        blob += s_log[idx];
        blob += '\n';
    }
    return blob;
}

static void writePersistent()
{
    Preferences p;
    if (!p.begin(NVS_NS, false)) return;
    p.putString(NVS_KEY, buildPersistBlob());
    p.end();
}

void loadPersistentEventLog()
{
    Preferences p;
    if (!p.begin(NVS_NS, true)) return;
    String blob = p.getString(NVS_KEY, "");
    p.end();
    if (blob.length() == 0) return;

    // Split by `\n` and push each entry into the ring. Any lines beyond
    // EVENT_LOG_SIZE naturally rotate — but by construction we only wrote
    // EVENT_LOG_PERSIST_SIZE which is <= EVENT_LOG_SIZE.
    int start = 0;
    while (start < (int)blob.length())
    {
        int end = blob.indexOf('\n', start);
        if (end < 0) end = blob.length();
        if (end > start)
            appendToRing(blob.substring(start, end));
        start = end + 1;
    }
}

void logEvent(const String &message)
{
    String line = "[" + logTimestamp() + "] " + message;
    appendToRing(line);
    writePersistent();
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

    Preferences p;
    if (p.begin(NVS_NS, false))
    {
        p.remove(NVS_KEY);
        p.end();
    }

    logEvent("Журнал очищен");
}
