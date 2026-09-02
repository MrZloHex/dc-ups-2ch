// SPDX-License-Identifier: GPL-3.0-or-later
#include "power.h"

#include "ups_common.h"
#include "event_log.h"
#include "ntfy.h"
#include "wifi_mgr.h"
#include "sleep_modes.h"
#include "recovery.h"

#include <WiFi.h>

bool          routerOn = false, ontOn = false;
bool          gridPresent = true, gridCandidate = true;
bool          warnSent = false;
bool          lvdTripped = false;
LoadMode      routerMode = LM_AUTO, ontMode = LM_AUTO;
BattState     battState = BATT_OK;
float         lastVbatt = 0.0f, lastVgrid = 0.0f;
unsigned long gridChangeSince = 0;
unsigned long routerTurnedOnAt = 0, ontTurnedOnAt = 0;
unsigned long outageStartedAt = 0;
unsigned long lastOutageDurationMs = 0;
uint32_t      outageCount = 0;

PowerCycleState powerCycleState     = PC_IDLE;
PowerTarget     powerCycleTarget    = TARGET_NONE;
unsigned long   powerCycleAt        = 0;
bool            powerCycleAutomatic = false;
String          powerCycleReason;

unsigned long rebootAt = 0;

static unsigned long s_lastTick = 0;

// ---------- Load control ----------

bool anyLoadOn()      { return routerOn || ontOn; }
bool networkLoadsOn() { return routerOn && ontOn; }

BattState getBattState(float vb)
{
    if (vb <= cfg.battCutoff) return BATT_CRIT;
    if (vb <= cfg.battWarn)   return BATT_WARN;
    return BATT_OK;
}

void setRouter(bool on)
{
    if (routerOn == on) return;
    routerOn = on;
    digitalWrite(PIN_LOAD_ROUTER, on ? HIGH : LOW);
    if (on)
    {
        routerTurnedOnAt = millis();
        logEvent("ROUTER -> ВКЛ");
    }
    else
        logEvent("ROUTER -> ВЫКЛ");
}

void setOnt(bool on)
{
    if (ontOn == on) return;
    ontOn = on;
    digitalWrite(PIN_LOAD_ONT, on ? HIGH : LOW);
    if (on)
    {
        ontTurnedOnAt = millis();
        logEvent("ONT -> ВКЛ");
    }
    else
        logEvent("ONT -> ВЫКЛ");
}

void setBothLoads(bool on)
{
    setRouter(on);
    setOnt(on);
}

void setTargetPower(PowerTarget target, bool on)
{
    if (targetIncludesRouter(target)) setRouter(on);
    if (targetIncludesOnt   (target)) setOnt   (on);
}

unsigned long equipmentTurnedOnAt()
{
    return routerTurnedOnAt > ontTurnedOnAt ? routerTurnedOnAt : ontTurnedOnAt;
}

bool canPowerLoadNow()
{
    // After LVD, require the restore threshold — don't let a manual restart
    // bypass the hysteresis on the battery's post-load-shed voltage bounce.
    bool batteryAllows = lvdTripped ? (lastVbatt >= cfg.battRestore)
                                    : (lastVbatt >  cfg.battCutoff);
    return gridPresent || batteryAllows;
}

static bool targetModeAllowsRestart(PowerTarget target, String &why)
{
    if (targetIncludesRouter(target) && routerMode == LM_OFF)
    {
        why = "ROUTER в ручном OFF.";
        return false;
    }
    if (targetIncludesOnt(target) && ontMode == LM_OFF)
    {
        why = "ONT в ручном OFF.";
        return false;
    }
    return true;
}

bool requestTargetRestart(PowerTarget target, String &answer, bool automatic,
                          const String &reason)
{
    if (target == TARGET_NONE)
    {
        answer = "Не выбран канал.";
        return false;
    }

    if (powerCycleState != PC_IDLE)
    {
        answer = "Перезапуск уже выполняется: " + targetText(powerCycleTarget) + ".";
        return false;
    }

    String why;
    if (!targetModeAllowsRestart(target, why))
    {
        answer = why + " Сначала выбери Авто или Вкл.";
        return false;
    }

    if (!canPowerLoadNow())
    {
        answer = "Перезапуск запрещён: сети нет, АКБ ниже отсечки.";
        return false;
    }

    powerCycleAutomatic = automatic;
    powerCycleReason    = reason;
    powerCycleTarget    = target;
    powerCycleState     = PC_WAIT_OFF;
    powerCycleAt        = millis() + (automatic ? 250UL : ACTION_RESPONSE_DELAY_MS);

    logEvent(String(automatic ? "AUTO RECOVERY: " : "Запрошен ручной restart: ") +
             targetText(target) + " — " + reason);

    answer = "ОК. " + targetText(target) + " будет отключён на 5 секунд.";
    return true;
}

bool requestRouterRestart(String &a) { return requestTargetRestart(TARGET_ROUTER, a, false, "ручной перезапуск ROUTER"); }
bool requestOntRestart   (String &a) { return requestTargetRestart(TARGET_ONT,    a, false, "ручной перезапуск ONT"); }
bool requestBothRestart  (String &a) { return requestTargetRestart(TARGET_BOTH,   a, false, "ручной перезапуск обоих каналов"); }

// ---------- Tick handlers ----------

void powerCycleTick()
{
    if (powerCycleState == PC_IDLE)             return;
    if ((long)(millis() - powerCycleAt) < 0)    return;

    if (powerCycleState == PC_WAIT_OFF)
    {
        logEvent("Restart " + targetText(powerCycleTarget) + ": снимаю питание");
        setTargetPower(powerCycleTarget, false);
        powerCycleState = PC_OFF_WAIT;
        powerCycleAt    = millis() + LOAD_RESTART_OFF_MS;
        return;
    }

    if (powerCycleState == PC_OFF_WAIT)
    {
        PowerTarget finishedTarget = powerCycleTarget;
        bool        wasAutomatic   = powerCycleAutomatic;
        String      finishedReason = powerCycleReason;

        powerCycleState     = PC_IDLE;
        powerCycleTarget    = TARGET_NONE;
        powerCycleAutomatic = false;
        powerCycleReason    = "";

        if (!canPowerLoadNow())
        {
            lvdTripped = true;
            logEvent("Restart " + targetText(finishedTarget) +
                     ": питание не возвращено — АКБ ниже отсечки");
            return;
        }

        if (targetIncludesRouter(finishedTarget) && routerMode != LM_OFF) setRouter(true);
        if (targetIncludesOnt   (finishedTarget) && ontMode    != LM_OFF) setOnt(true);
        lvdTripped = false;

        if (targetIncludesRouter(finishedTarget))
        {
            nextWifiConnectReason = wasAutomatic
                ? "Автовосстановление: после перезапуска ROUTER WiFi восстановлен."
                : "После перезапуска ROUTER WiFi восстановлен.";
            wifiStatusText = "ждём загрузку роутера";
            lastReconnect  = millis();
        }

        // Force the recovery module to re-measure connectivity.
        networkBadSince      = millis();
        lastInternetCheck    = 0;
        internetHealthySince = 0;

        logEvent("Restart " + targetText(finishedTarget) +
                 ": питание возвращено (" + finishedReason + ")");
    }
}

void rebootTick()
{
    if (rebootAt != 0 && (long)(millis() - rebootAt) >= 0)
    {
        logEvent("Перезагрузка ESP");
        delay(100);
        ESP.restart();
    }
}

// ---------- Protection tick: measure, hysteresis, LVD ----------

static void applyChannelMode(bool isRouter, float vb)
{
    LoadMode modeRef = isRouter ? routerMode : ontMode;
    bool     onNow   = isRouter ? routerOn   : ontOn;

    if (modeRef == LM_OFF)
    {
        if (onNow)
        {
            if (isRouter) setRouter(false); else setOnt(false);
        }
        return;
    }

    bool batteryAllows = lvdTripped ? (vb >= cfg.battRestore)
                                    : (vb >  cfg.battCutoff);
    bool want = gridPresent || batteryAllows;

    if (want != onNow)
    {
        if (isRouter) setRouter(want); else setOnt(want);
    }
}

void protectTick()
{
    if (millis() - s_lastTick < 1000) return;
    s_lastTick = millis();

    float vb = readVoltage(PIN_VBATT, DIV_VBATT, cfg.calVbatt);
    float vg = readVoltage(PIN_VGRID, DIV_VGRID, cfg.calVgrid);

    lastVbatt = vb;
    lastVgrid = vg;
    battState = getBattState(vb);

    // Grid presence hysteresis.
    bool nowGrid = gridPresent ? (vg > cfg.gridOff) : (vg > cfg.gridOn);
    if (nowGrid != gridCandidate)
    {
        gridCandidate   = nowGrid;
        gridChangeSince = millis();
    }

    if (nowGrid != gridPresent && millis() - gridChangeSince > cfg.gridDebounce)
    {
        gridPresent = nowGrid;

        if (gridPresent)
        {
            if (outageStartedAt != 0)
            {
                lastOutageDurationMs = millis() - outageStartedAt;
                outageStartedAt      = 0;
            }

            logEvent("Сеть появилась. АКБ " + String(vb, 1) + " В" +
                     (lastOutageDurationMs > 0
                        ? ", отключение длилось " + formatDuration(lastOutageDurationMs)
                        : ""));

            lvdTripped    = false;
            sleepLowSince = 0;

            if (powerCycleState == PC_IDLE)
            {
                if (routerMode != LM_OFF) setRouter(true);
                if (ontMode    != LM_OFF) setOnt(true);
            }

            String msg = "Свет дали. АКБ " + String(vb, 1) + " В";
            if (lastOutageDurationMs > 0)
                msg += "\nБез сети: " + formatDuration(lastOutageDurationMs);
            sendNtfy(msg);
            warnSent = false;
        }
        else
        {
            outageStartedAt = millis();
            outageCount++;
            logEvent("Сеть пропала. Переход на АКБ " + String(vb, 1) + " В");
            sendNtfy("Свет отключили! Работа от АКБ, " + String(vb, 1) + " В");
        }
    }

    // Shared LVD: send the notification first (while ROUTER still runs),
    // then drop both channels.
    if (!gridPresent && !lvdTripped && vb <= cfg.battCutoff && anyLoadOn() &&
        powerCycleState == PC_IDLE)
    {
        sendNtfy("АКБ разряжена (" + String(vb, 1) +
                 " В). ROUTER и ONT отключаются; затем ESP уйдёт в deep sleep.");
        logEvent("LVD: АКБ " + String(vb, 2) + " В -> отключаю ROUTER+ONT");
        setBothLoads(false);
        lvdTripped    = true;
        sleepLowSince = millis();
    }

    // Apply manual OFF/ON/AUTO per-channel, but don't touch a channel that's
    // currently in a power-cycle.
    if (powerCycleState == PC_IDLE)
    {
        if (gridPresent)
        {
            lvdTripped = false;
            applyChannelMode(true,  vb);
            applyChannelMode(false, vb);
        }
        else if (!lvdTripped)
        {
            applyChannelMode(true,  vb);
            applyChannelMode(false, vb);
        }
        else if (vb >= cfg.battRestore)
        {
            // LVD recovery hysteresis.
            lvdTripped    = false;
            sleepLowSince = 0;
            applyChannelMode(true,  vb);
            applyChannelMode(false, vb);
        }
    }

    if (!gridPresent && anyLoadOn() && vb <= cfg.battWarn && !warnSent)
    {
        sendNtfy("АКБ садится: " + String(vb, 1) + " В. Скоро отключение ROUTER+ONT.");
        warnSent = true;
    }
    if (vb > cfg.battWarn + 0.3f)
        warnSent = false;

    Serial.printf("Vbat=%.2f V24=%.2f grid=%d router=%d ont=%d rMode=%d oMode=%d state=%d wifi=%d\n",
                  vb, vg, gridPresent, routerOn, ontOn,
                  (int)routerMode, (int)ontMode, (int)battState,
                  WiFi.status() == WL_CONNECTED);
}
