// SPDX-License-Identifier: MIT
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

// Используется на timer-wakeup ДО инициализации WiFi/lwIP.
// Никаких WiFi.* здесь вызывать нельзя.
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
    // К этому моменту основная нагрузка уже должна быть снята LVD.
    // Если WiFi ещё каким-то образом доступен — пробуем отправить последнее сообщение,
    // но не задерживаем сон надолго.
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

    // Подтверждение: три коротких мигания красным.
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

    // Нельзя входить в EXT0 sleep, пока кнопка всё ещё удерживается LOW:
    // иначе контроллер мгновенно проснётся. Ждём отпускания.
    while (digitalRead(PIN_BUTTON) == LOW)
    {
        esp_task_wdt_reset();
        delay(20);
    }
    delay(80);

    rtcEmergencySleep = false;
    rtcShelfSleep     = true;
    rtcSleepWakeCount = 0;

    // GPIO14 — RTC GPIO у классического ESP32. В режиме хранения таймер НЕ включаем:
    // единственный источник пробуждения — кнопка, замыкающая GPIO14 на GND.
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

    // Основной сценарий: LVD уже отключил оба выхода по battCutoff.
    // После снятия нагрузки напряжение АКБ отскакивает вверх, поэтому НЕ ждём,
    // пока оно снова упадёт до battSleep. battSleep остаётся аварийным резервным порогом.
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
