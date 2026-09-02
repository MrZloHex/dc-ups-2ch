// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_store.h"
#include "ups_common.h"
#include "ntfy.h"
#include "recovery.h"

void loadConfig()
{
    prefs.begin("ups", true);

    cfg.ssid      = prefs.getString("ssid", "");
    cfg.pass      = prefs.getString("pass", "");
    cfg.ntfy      = prefs.getString("ntfy", "");
    cfg.apPass    = prefs.getString("apPass",    AP_DEFAULT_PASSWORD);
    cfg.adminPass = prefs.getString("adminPass", "");

    cfg.calVbatt = prefs.getFloat("calVbatt", 1.0f);
    cfg.calVgrid = prefs.getFloat("calVgrid", 1.0f);

    cfg.battCutoff  = prefs.getFloat("cutoff",  11.0f);
    cfg.battRestore = prefs.getFloat("restore", 12.5f);
    cfg.battWarn    = prefs.getFloat("warn",    11.5f);
    cfg.battSleep   = prefs.getFloat("sleepV",  10.8f);

    cfg.gridOn       = prefs.getFloat("gridOn",  15.0f);
    cfg.gridOff      = prefs.getFloat("gridOff", 10.0f);
    cfg.gridDebounce = prefs.getUInt("deb", 3000);

    cfg.deepSleepEnabled = prefs.getBool("deepSleep", true);
    cfg.sleepCheckSec    = prefs.getUInt("sleepChk",   30);
    cfg.sleepDelaySec    = prefs.getUInt("sleepDelay", 30);

    cfg.autoRecoveryEnabled  = prefs.getBool("autoRec", true);
    cfg.autoEspRebootEnabled = prefs.getBool("autoEsp", false);
    cfg.netBadSec            = prefs.getUInt("netBadSec",  180);
    cfg.portalIdleSec        = prefs.getUInt("portalIdle", 900);

    // Rarely-updated counters; survive software reset via NVS.
    rtcAutoPowerCycles = prefs.getUChar("recCycles",  rtcAutoPowerCycles);
    rtcNetReboots      = prefs.getUChar("recReboots", rtcNetReboots);

    ntfyLastCommandId = prefs.getString("ntfyCmdId", "");
    ntfyLastSeenId    = ntfyLastCommandId;

    // Clamp any accidentally-saved absurd values.
    if (cfg.battSleep > cfg.battCutoff - 0.1f) cfg.battSleep = cfg.battCutoff - 0.1f;
    if (cfg.battSleep < 9.0f)                  cfg.battSleep = 9.0f;
    if (cfg.sleepCheckSec < 10)                cfg.sleepCheckSec = 10;
    if (cfg.sleepCheckSec > 3600)              cfg.sleepCheckSec = 3600;
    if (cfg.sleepDelaySec > 600)               cfg.sleepDelaySec = 600;
    if (cfg.netBadSec < 60)                    cfg.netBadSec = 60;
    if (cfg.netBadSec > 3600)                  cfg.netBadSec = 3600;
    if (cfg.portalIdleSec < 60)                cfg.portalIdleSec = 60;
    if (cfg.portalIdleSec > 86400)             cfg.portalIdleSec = 86400;

    prefs.end();
}

void saveConfig()
{
    prefs.begin("ups", false);

    prefs.putString("ssid",      cfg.ssid);
    prefs.putString("pass",      cfg.pass);
    prefs.putString("ntfy",      cfg.ntfy);
    prefs.putString("apPass",    cfg.apPass);
    prefs.putString("adminPass", cfg.adminPass);

    prefs.putFloat("calVbatt", cfg.calVbatt);
    prefs.putFloat("calVgrid", cfg.calVgrid);

    prefs.putFloat("cutoff",  cfg.battCutoff);
    prefs.putFloat("restore", cfg.battRestore);
    prefs.putFloat("warn",    cfg.battWarn);
    prefs.putFloat("sleepV",  cfg.battSleep);

    prefs.putFloat("gridOn",  cfg.gridOn);
    prefs.putFloat("gridOff", cfg.gridOff);
    prefs.putUInt ("deb",     cfg.gridDebounce);

    prefs.putBool("deepSleep",  cfg.deepSleepEnabled);
    prefs.putUInt("sleepChk",   cfg.sleepCheckSec);
    prefs.putUInt("sleepDelay", cfg.sleepDelaySec);

    prefs.putBool("autoRec",    cfg.autoRecoveryEnabled);
    prefs.putBool("autoEsp",    cfg.autoEspRebootEnabled);
    prefs.putUInt("netBadSec",  cfg.netBadSec);
    prefs.putUInt("portalIdle", cfg.portalIdleSec);

    prefs.end();
}

void saveRecoveryCounters()
{
    prefs.begin("ups", false);
    prefs.putUChar("recCycles",  rtcAutoPowerCycles);
    prefs.putUChar("recReboots", rtcNetReboots);
    prefs.end();
}

void saveNtfyCommandId(const String &id)
{
    if (id == "" || id == ntfyLastCommandId) return;
    ntfyLastCommandId = id;
    prefs.begin("ups", false);
    prefs.putString("ntfyCmdId", id);
    prefs.end();
}
