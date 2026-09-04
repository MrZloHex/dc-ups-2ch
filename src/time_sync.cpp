// SPDX-License-Identifier: GPL-3.0-or-later
#include "time_sync.h"

#include <time.h>

static bool s_configured = false;

void ntpBegin()
{
    if (s_configured) return;
    // configTime() calls sntp_setservername + sntp_init under the hood. It's
    // safe to call before WiFi is up — the sync just won't happen until the
    // stack has an IP. We still gate it behind s_configured to avoid re-init
    // spam on every reconnect.
    configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com");
    s_configured = true;
}

bool timeSynced()
{
    // Any epoch newer than 2024-01-01 is a real sync — the ESP32 RTC starts
    // at epoch 0 on cold boot, so this test is unambiguous.
    time_t now = time(nullptr);
    return now > 1704067200L;
}

String wallClockNow()
{
    if (!timeSynced()) return String();

    time_t    now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    return String(buf);
}

String wallClockShort()
{
    if (!timeSynced()) return String();

    time_t    now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    char buf[12];
    strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    return String(buf);
}
