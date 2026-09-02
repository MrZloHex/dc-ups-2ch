// SPDX-License-Identifier: GPL-3.0-or-later
//
// DC-UPS-2CH — ESP32 backup power controller for network gear.
//   - two independent DC outputs (ROUTER, ONT) via high-side P-MOSFETs;
//   - battery / 24 V sensing on ADC1 (100k/10k dividers, x11);
//   - web panel, mDNS, ntfy (outgoing + read-only inbound commands);
//   - emergency deep sleep (timer wake) + Shelf Sleep (button-only wake);
//   - staged auto-recovery: probe -> power-cycle -> optional ESP reboot.
//
// No external Arduino libraries: everything ships in the ESP32 core.
// Wiring, GPIO map, BOM: docs/HARDWARE.md.
// Full Russian passport: docs/DC_UPS_documentation_v6.tex.

#include <Arduino.h>
#include "esp_task_wdt.h"
#include "esp_sleep.h"

#include "ups_common.h"
#include "config_store.h"
#include "event_log.h"
#include "hardware.h"
#include "power.h"
#include "wifi_mgr.h"
#include "ntfy.h"
#include "recovery.h"
#include "sleep_modes.h"
#include "web_ui.h"

void setup()
{
    Serial.begin(115200);

    esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

    initHardware();

    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);

    loadConfig();

    // First measurement before WiFi — also decides whether to go straight
    // back into emergency sleep on a low battery.
    lastVbatt = readVoltage(PIN_VBATT, DIV_VBATT, cfg.calVbatt);
    lastVgrid = readVoltage(PIN_VGRID, DIV_VGRID, cfg.calVgrid);

    battState       = getBattState(lastVbatt);
    gridPresent     = lastVgrid > cfg.gridOn;
    gridCandidate   = gridPresent;
    gridChangeSince = millis();

    handleWakeCause(wakeCause);

    bool wokeFromEmergencySleep =
        (wakeCause == ESP_SLEEP_WAKEUP_TIMER && rtcEmergencySleep);

    if (wokeFromEmergencySleep)
    {
        rtcSleepWakeCount++;

        // In emergency mode we do NOT bring WiFi up every 30 s — just measure
        // and sleep again. Full boot only when grid is back or battery recovered.
        if (!gridPresent && lastVbatt < cfg.battRestore)
        {
            Serial.printf("Emergency wake #%lu: Vbat=%.2f V24=%.2f -> sleep again\n",
                          (unsigned long)rtcSleepWakeCount, lastVbatt, lastVgrid);
            emergencySleepTimerOnly();
        }

        uint32_t sleptChecks = rtcSleepWakeCount;
        rtcEmergencySleep    = false;
        rtcSleepWakeCount    = 0;

        nextWifiConnectReason =
            "ИБП проснулся после аварийного сна. Проверок во сне: " +
            String(sleptChecks) + ".";
    }
    else if (cfg.deepSleepEnabled && !gridPresent && lastVbatt <= cfg.battSleep)
    {
        // Cold-booted with a deeply-discharged battery — don't bring the
        // radio up, don't finish killing the battery.
        rtcEmergencySleep = true;
        rtcShelfSleep     = false;
        rtcSleepWakeCount = 0;
        Serial.printf("Cold boot with low battery %.2f V -> emergency sleep\n", lastVbatt);
        emergencySleepTimerOnly();
    }

    logEvent("BOOT FW " FW_FULL_ID +
             String(" | АКБ ") + String(lastVbatt, 2) +
             " В | 24В "        + String(lastVgrid, 2) +
             " В | сеть "       + (gridPresent ? "есть" : "нет"));

    if (!gridPresent)
    {
        outageStartedAt = millis();
        outageCount     = 1;
    }

    // KEY POINT: the ESP controls power to the router whose WiFi it must join.
    // Turn the loads on first, wait for the router to boot, then try WiFi.
    bool canPowerRouter = gridPresent || (lastVbatt > cfg.battCutoff);
    if (canPowerRouter)
    {
        setRouter(true);
        setOnt(true);
    }
    else
    {
        setBothLoads(false);
        // Booted below LVD but above the fallback sleep threshold — treat LVD
        // as tripped so the emergency-sleep timer starts.
        if (!gridPresent && lastVbatt <= cfg.battCutoff)
        {
            lvdTripped    = true;
            sleepLowSince = millis();
        }
    }

    if (cfg.ssid == "")
    {
        wifiStatusText = "SSID не задан";
        startPortal();
    }
    else if (!routerOn)
    {
        wifiStatusText = "роутер отключён: АКБ ниже отсечки";
        startPortal();
    }
    else
    {
        Serial.printf("Waiting %lu ms for ROUTER/ONT boot...\n",
                      (unsigned long)ROUTER_BOOT_MS);
        waitWithWatchdog(ROUTER_BOOT_MS);

        if (connectSTABlocking(WIFI_CONNECT_TIMEOUT_MS))
        {
            wifiWasConnected = true;
            handleWifiConnected();
        }
        else
        {
            wifiWasConnected = false;
            wifiStatusText   = "WiFi не найден; портал активен";
            logEvent("WiFi не найден -> запускаю setup portal");
            startPortal();
        }
    }

    // WiFi is now initialised (STA via connectSTABlocking, or AP+STA via
    // startPortal). Safe to open the TCP socket for the WebServer.
    beginWeb();

    updateLeds();
}

void loop()
{
    esp_task_wdt_reset();

    protectTick();
    powerCycleTick();
    buttonTick();
    deepSleepTick();
    rebootTick();
    updateLeds();

    // The WebServer runs on both home WiFi and the setup AP.
    server.handleClient();

    if (portalActive)
        dns.processNextRequest();

    wifiRetryTick();
    wifiTick();
    portalMaintenanceTick();
    networkRecoveryTick();
    ntfyTick();
    ntfyCommandTick();
}
