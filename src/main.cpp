// SPDX-License-Identifier: MIT
//
// DC-UPS-2CH — источник резервного питания сетевого оборудования на ESP32.
//   • два независимых DC-выхода (ROUTER, ONT) через P-MOSFET,
//   • контроль АКБ / 24 В через ADC1 (делители 100k/10k -> x11),
//   • веб-панель, mDNS, ntfy (исходящие уведомления + read-only команды),
//   • аварийный deep sleep по таймеру + Shelf Sleep только по кнопке,
//   • автоматическое восстановление связи (probe -> power-cycle -> reboot).
//
// Внешних библиотек не требуется: только Arduino-framework для ESP32.
// Схема, распиновка и BOM: см. docs/HARDWARE.md.
// Полный русский паспорт: docs/DC_UPS_documentation_v6.tex.

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

    // Первичный замер до старта WiFi: он же определяет, нужен ли сразу аварийный сон.
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

        // В аварийном режиме не поднимаем WiFi каждые 30 секунд.
        // Только быстро меряем АКБ/24В. Полный запуск — когда вернулась сеть
        // или аккумулятор действительно восстановился.
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
        // Если устройство включили уже с глубоко разряженным АКБ —
        // не поднимаем радио и не добиваем батарею.
        rtcEmergencySleep = true;
        rtcShelfSleep     = false;
        rtcSleepWakeCount = 0;
        Serial.printf("Cold boot with low battery %.2f V -> emergency sleep\n", lastVbatt);
        emergencySleepTimerOnly();
    }

    logEvent("BOOT FW " FW_VERSION +
             String(" | АКБ ") + String(lastVbatt, 2) +
             " В | 24В "        + String(lastVgrid, 2) +
             " В | сеть "       + (gridPresent ? "есть" : "нет"));

    if (!gridPresent)
    {
        outageStartedAt = millis();
        outageCount     = 1;
    }

    // КЛЮЧЕВОЕ: ESP управляет питанием роутера, к WiFi которого сама должна
    // подключиться. Поэтому сначала включаем нагрузку, ждём загрузку роутера,
    // и только затем пытаемся подключиться к WiFi.
    bool canPowerRouter = gridPresent || (lastVbatt > cfg.battCutoff);
    if (canPowerRouter)
    {
        setRouter(true);
        setOnt(true);
    }
    else
    {
        setBothLoads(false);
        // Если ESP загрузилась уже ниже LVD (но чуть выше резервного sleepV),
        // считаем LVD сработавшим: через sleepDelaySec она тоже уйдёт в сон.
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

    // К этому моменту WiFi уже инициализирован:
    // либо STA через connectSTABlocking(), либо AP+STA через startPortal().
    // Теперь безопасно открывать TCP-сокет WebServer.
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

    // WebServer работает и через домашний WiFi, и через setup AP.
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
