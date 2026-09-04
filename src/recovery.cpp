// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery.h"

#include "ups_common.h"
#include "event_log.h"
#include "power.h"
#include "wifi_mgr.h"
#include "config_store.h"
#include "boot_flow.h"

#include <WiFi.h>
#include "esp_task_wdt.h"

RTC_DATA_ATTR uint8_t rtcAutoPowerCycles = 0;
RTC_DATA_ATTR uint8_t rtcNetReboots      = 0;

unsigned long lastInternetCheck    = 0;
unsigned long networkBadSince      = 0;
unsigned long internetHealthySince = 0;
bool          internetKnown        = false;
bool          internetReachable    = false;
String        recoveryStatus       = "ожидание";

bool tcpProbe(IPAddress ip, uint16_t port)
{
    WiFiClient c;
    c.setTimeout(1500);
    bool ok = c.connect(ip, port);
    c.stop();
    return ok;
}

bool checkInternetReachable()
{
    if (WiFi.status() != WL_CONNECTED)
        return false;

    // Two independent endpoints. Only a TCP connect — no HTTP traffic. Less
    // dependent on ntfy and doesn't spam anyone.
    if (tcpProbe(IPAddress(1, 1, 1, 1), 443))
        return true;

    esp_task_wdt_reset();

    if (tcpProbe(IPAddress(8, 8, 8, 8), 53))
        return true;

    return false;
}

static bool startAutomaticPowerCycle(PowerTarget target, const String &reason)
{
    if (powerCycleState != PC_IDLE || !canPowerLoadNow())
        return false;

    String answer;
    if (!requestTargetRestart(target, answer, true, reason))
        return false;

    rtcAutoPowerCycles++;
    saveRecoveryCounters();
    recoveryStatus = "автоперезапуск " + targetText(target) + " " +
                     String(rtcAutoPowerCycles) + "/" + String(AUTO_MAX_POWER_CYCLES);

    logEvent("AUTO RECOVERY: " + reason + ", цикл " +
             String(rtcAutoPowerCycles) + "/" + String(AUTO_MAX_POWER_CYCLES) +
             ", цель " + targetText(target));

    networkBadSince      = 0;
    internetHealthySince = 0;
    return true;
}

void networkRecoveryTick()
{
    if (!cfg.autoRecoveryEnabled)
    {
        recoveryStatus = "автовосстановление выключено";
        return;
    }

    if (rebootAt != 0 || powerCycleState != PC_IDLE)
        return;

    if (!networkLoadsOn() || routerMode == LM_OFF || ontMode == LM_OFF)
    {
        recoveryStatus       = "ROUTER/ONT выключен";
        networkBadSince      = 0;
        internetHealthySince = 0;
        return;
    }

    if (portalActive)
    {
        recoveryStatus = "setup portal";
        return;
    }

    // Don't burn the last of the battery on recovery loops.
    if (!gridPresent && lastVbatt <= cfg.battWarn)
    {
        recoveryStatus = "пауза: низкий АКБ";
        return;
    }

    // Don't diagnose anything while the router / ONT are booting.
    if (millis() - equipmentTurnedOnAt() < ROUTER_BOOT_MS + 10000UL)
    {
        recoveryStatus = "ждём загрузку оборудования";
        return;
    }

    bool   badNow = false;
    String badReason;

    if (WiFi.status() != WL_CONNECTED)
    {
        badNow            = true;
        internetKnown     = false;
        internetReachable = false;
        badReason         = "ESP не подключена к WiFi";
    }
    else if (lastInternetCheck == 0 || millis() - lastInternetCheck >= INTERNET_CHECK_MS)
    {
        lastInternetCheck = millis();
        bool ok = checkInternetReachable();

        if (ok)
        {
            if (!internetKnown || !internetReachable)
                logEvent("Internet probe: связь есть");

            internetKnown     = true;
            internetReachable = true;
            networkBadSince   = 0;
            recoveryStatus    = "интернет есть";

            if (internetHealthySince == 0)
                internetHealthySince = millis();

            if (millis() - internetHealthySince >= HEALTHY_RESET_MS &&
                (rtcAutoPowerCycles != 0 || rtcNetReboots != 0))
            {
                logEvent("AUTO RECOVERY: счётчики сброшены после 10 мин стабильной связи");
                rtcAutoPowerCycles = 0;
                rtcNetReboots      = 0;
                saveRecoveryCounters();
            }

            return;
        }

        if (!internetKnown || internetReachable)
            logEvent("Internet probe: интернет не отвечает");

        internetKnown     = true;
        internetReachable = false;
        badNow            = true;
        badReason         = "WiFi есть, но интернет не отвечает";
    }
    else
    {
        // Between probe intervals, keep the last-known state.
        if (internetKnown && !internetReachable)
        {
            badNow    = true;
            badReason = "интернет не отвечает";
        }
        else
        {
            return;
        }
    }

    if (!badNow) return;

    internetHealthySince = 0;

    if (networkBadSince == 0)
    {
        networkBadSince = millis();
        recoveryStatus  = "проблема связи: " + badReason;
        logEvent("AUTO RECOVERY: начался таймер — " + badReason);
        return;
    }

    uint32_t badFor = millis() - networkBadSince;
    recoveryStatus  = "нет связи " + formatDuration(badFor);

    if (badFor < cfg.netBadSec * 1000UL)
        return;

    if (rtcAutoPowerCycles < AUTO_MAX_POWER_CYCLES)
    {
        // Selection logic lives in boot_flow.cpp (pure, native-tested):
        //   cycle 0: failing layer only (ROUTER if Wi-Fi is down, ONT if WAN);
        //   cycle 1+: always both channels together.
        PowerTarget target = (PowerTarget)pickRecoveryTarget(
            rtcAutoPowerCycles,
            WiFi.status() == WL_CONNECTED);

        startAutomaticPowerCycle(target, badReason);
        return;
    }

    if (cfg.autoEspRebootEnabled && rtcNetReboots < AUTO_MAX_ESP_REBOOTS)
    {
        rtcNetReboots++;
        saveRecoveryCounters();
        logEvent("AUTO RECOVERY: power-cycle не помог, запланирован reboot ESP");
        recoveryStatus  = "перезагрузка ESP";
        rebootAt        = millis() + 1500UL;
        networkBadSince = 0;
        return;
    }

    recoveryStatus = "лимит autorecovery исчерпан";
}
