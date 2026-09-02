// SPDX-License-Identifier: GPL-3.0-or-later
#include "sleep_modes.h"

#include "ups_common.h"
#include "event_log.h"
#include "power.h"
#include "wifi_mgr.h"
#include "ntfy.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "driver/rtc_io.h"

RTC_DATA_ATTR bool     rtcEmergencySleep = false;
RTC_DATA_ATTR bool     rtcShelfSleep     = false;
RTC_DATA_ATTR uint32_t rtcSleepWakeCount = 0;

unsigned long sleepLowSince = 0;

// Called on timer wake BEFORE WiFi/lwIP init. Must not touch WiFi.*
[[noreturn]] void emergencySleepTimerOnly()
{
    digitalWrite(PIN_LOAD_ROUTER, LOW);
    digitalWrite(PIN_LOAD_ONT,    LOW);
    routerOn = false;
    ontOn    = false;
    digitalWrite(PIN_LED_GRID, LOW);
    digitalWrite(PIN_LED_BATT, LOW);

    rtcEmergencySleep = true;
    rtcShelfSleep     = false;
    esp_sleep_enable_timer_wakeup((uint64_t)cfg.sleepCheckSec * 1000000ULL);

    Serial.printf("EMERGENCY SLEEP: wake in %lu s\n",
                  (unsigned long)cfg.sleepCheckSec);
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();

    while (true) delay(1000);
}

[[noreturn]] void enterEmergencySleep(const String &reason)
{
    // Loads should already be off (LVD). If WiFi still works, try one last
    // notification, but do not delay sleep for long.
    logEvent("Аварийный deep sleep: " + reason);

    if (WiFi.status() == WL_CONNECTED && cfg.ntfy != "")
    {
        sendNtfy("ИБП уходит в аварийный сон.\nПричина: " + reason +
                 "\nАКБ: " + String(lastVbatt, 2) + " В" +
                 "\nПроверка сети каждые " + String(cfg.sleepCheckSec) + " с.");
    }

    setBothLoads(false);
    digitalWrite(PIN_LED_GRID, LOW);
    digitalWrite(PIN_LED_BATT, LOW);

    if (mdnsStarted)
    {
        MDNS.end();
        mdnsStarted = false;
    }

    if (portalActive)
    {
        stopPortal();
    }

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    delay(100);

    rtcEmergencySleep = true;
    rtcShelfSleep     = false;
    rtcSleepWakeCount = 0;
    esp_sleep_enable_timer_wakeup((uint64_t)cfg.sleepCheckSec * 1000000ULL);

    Serial.printf("Deep sleep: Vbat=%.2f, check every %lu s\n",
                  lastVbatt, (unsigned long)cfg.sleepCheckSec);
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();

    while (true) delay(1000);
}

[[noreturn]] void enterShelfSleep()
{
    logEvent("Режим хранения: выключаю всё и засыпаю до нажатия кнопки");

    if (WiFi.status() == WL_CONNECTED && cfg.ntfy != "")
        sendNtfy("DC-UPS переведён в режим хранения.\nROUTER/ONT: OFF\nПробуждение: нажать кнопку на устройстве.");

    // Feedback: three short red blinks.
    for (uint8_t i = 0; i < 3; ++i)
    {
        digitalWrite(PIN_LED_BATT, HIGH);
        delay(120);
        digitalWrite(PIN_LED_BATT, LOW);
        delay(120);
        esp_task_wdt_reset();
    }

    setBothLoads(false);
    digitalWrite(PIN_LED_GRID, LOW);
    digitalWrite(PIN_LED_BATT, LOW);

    if (mdnsStarted)
    {
        MDNS.end();
        mdnsStarted = false;
    }
    if (portalActive)
        stopPortal();

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    delay(100);

    // Don't enter EXT0 sleep while the button is still LOW — the chip would
    // immediately wake back up. Wait for release.
    while (digitalRead(PIN_BUTTON) == LOW)
    {
        esp_task_wdt_reset();
        delay(20);
    }
    delay(80);

    rtcEmergencySleep = false;
    rtcShelfSleep     = true;
    rtcSleepWakeCount = 0;

    // GPIO14 is an RTC-GPIO on the classic ESP32. In shelf mode the timer is
    // NOT enabled — the only wake source is the button (GPIO14 -> GND).
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    rtc_gpio_init         ((gpio_num_t)PIN_BUTTON);
    rtc_gpio_set_direction((gpio_num_t)PIN_BUTTON, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en    ((gpio_num_t)PIN_BUTTON);
    rtc_gpio_pulldown_dis ((gpio_num_t)PIN_BUTTON);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON, 0);

    Serial.println("SHELF SLEEP: wake only by GPIO14 button");
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();
    while (true) delay(1000);
}

void deepSleepTick()
{
    if (!cfg.deepSleepEnabled)
    {
        sleepLowSince = 0;
        return;
    }

    if (gridPresent || anyLoadOn() || powerCycleState != PC_IDLE || rebootAt != 0)
    {
        sleepLowSince = 0;
        return;
    }

    // Main path: LVD already dropped both outputs at battCutoff. Once
    // unloaded, the battery voltage bounces back up, so we DON'T wait for it
    // to fall to battSleep — battSleep is only the fallback threshold.
    bool afterLvd         = lvdTripped;
    bool emergencyVoltage = lastVbatt <= cfg.battSleep;

    if (!afterLvd && !emergencyVoltage)
    {
        sleepLowSince = 0;
        return;
    }

    if (sleepLowSince == 0)
    {
        sleepLowSince = millis();
        if (afterLvd)
            logEvent("Deep sleep: LVD снял нагрузку; ожидание " +
                     String(cfg.sleepDelaySec) + " с перед сном");
        else
            logEvent("АКБ ниже аварийного порога сна: " + String(lastVbatt, 2) +
                     " В <= " + String(cfg.battSleep, 2) + " В");
        return;
    }

    if (millis() - sleepLowSince < cfg.sleepDelaySec * 1000UL)
        return;

    String reason = afterLvd
        ? ("LVD отключил ROUTER+ONT; АКБ " + String(lastVbatt, 2) + " В")
        : ("АКБ " + String(lastVbatt, 2) + " В ниже аварийного порога");
    enterEmergencySleep(reason + ", сеть отсутствует");
}
