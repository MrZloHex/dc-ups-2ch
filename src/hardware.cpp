// SPDX-License-Identifier: MIT
#include "hardware.h"

#include "ups_common.h"
#include "event_log.h"
#include "power.h"
#include "ntfy.h"
#include "wifi_mgr.h"
#include "sleep_modes.h"

#include "esp_task_wdt.h"
#include "driver/rtc_io.h"

bool buttonIgnoreUntilRelease = false;

// Локальное состояние конечного автомата кнопки.
static bool          s_raw            = HIGH;
static bool          s_stable         = HIGH;
static bool          s_longHandled    = false;
static uint8_t       s_clickCount     = 0;
static unsigned long s_rawChangedAt   = 0;
static unsigned long s_pressedAt      = 0;
static unsigned long s_firstClickAt   = 0;

void initHardware()
{
    // После EXT0 deep sleep RTC-пин остаётся в RTC-режиме;
    // возвращаем GPIO14 обычному GPIO driver.
    rtc_gpio_deinit((gpio_num_t)PIN_BUTTON);

    pinMode(PIN_LED_GRID,    OUTPUT);
    pinMode(PIN_LED_BATT,    OUTPUT);
    pinMode(PIN_LOAD_ROUTER, OUTPUT);
    pinMode(PIN_LOAD_ONT,    OUTPUT);
    pinMode(PIN_BUTTON,      INPUT_PULLUP);

    // При старте оба силовых ключа гарантированно выключены.
    digitalWrite(PIN_LOAD_ROUTER, LOW);
    digitalWrite(PIN_LOAD_ONT,    LOW);
    routerOn = false;
    ontOn    = false;

    s_raw    = digitalRead(PIN_BUTTON);
    s_stable = s_raw;

    // Короткий self-test LED.
    digitalWrite(PIN_LED_GRID, HIGH);
    digitalWrite(PIN_LED_BATT, HIGH);
    delay(400);
    digitalWrite(PIN_LED_GRID, LOW);
    digitalWrite(PIN_LED_BATT, LOW);

    analogReadResolution(12);
    analogSetPinAttenuation(PIN_VBATT, ADC_11db);
    analogSetPinAttenuation(PIN_VGRID, ADC_11db);
}

void handleWakeCause(esp_sleep_wakeup_cause_t cause)
{
    if (cause == ESP_SLEEP_WAKEUP_EXT0 && rtcShelfSleep)
    {
        rtcShelfSleep        = false;
        rtcEmergencySleep    = false;
        rtcSleepWakeCount    = 0;
        buttonIgnoreUntilRelease = true;
        nextWifiConnectReason = "DC-UPS включён кнопкой из режима хранения.";
        Serial.println("Wake from SHELF SLEEP by GPIO14 button");
    }
}

void updateLeds()
{
    digitalWrite(PIN_LED_GRID, gridPresent ? HIGH : LOW);

    bool red;
    if (battState == BATT_CRIT || (!gridPresent && !anyLoadOn()))
        red = true;
    else if (gridPresent && battState == BATT_OK)
        red = false;
    else
    {
        const int period = (battState == BATT_WARN) ? 250 : 1000;
        red = ((millis() / period) % 2) != 0;
    }

    digitalWrite(PIN_LED_BATT, red ? HIGH : LOW);
}

static void handleButtonShort()
{
    logEvent("Кнопка: короткое -> запрос статуса ntfy");
    bool ok = sendNtfy(buildStatusMessage("Статус DC-UPS по кнопке"));

    // 2 зелёных = отправлено; 3 красных = WiFi/ntfy недоступны.
    uint8_t count = ok ? 2 : 3;
    int     pin   = ok ? PIN_LED_GRID : PIN_LED_BATT;
    for (uint8_t i = 0; i < count; ++i)
    {
        digitalWrite(pin, HIGH);
        delay(90);
        digitalWrite(pin, LOW);
        delay(90);
        esp_task_wdt_reset();
    }
    updateLeds();
}

static void handleButtonDouble()
{
    startManualPortalFromButton();
}

void buttonTick()
{
    bool          raw = digitalRead(PIN_BUTTON);
    unsigned long now = millis();

    // После EXT0 wake кнопка ещё может физически удерживаться.
    // Не считаем это новым нажатием.
    if (buttonIgnoreUntilRelease)
    {
        if (raw == HIGH)
        {
            buttonIgnoreUntilRelease = false;
            s_raw          = HIGH;
            s_stable       = HIGH;
            s_clickCount   = 0;
            s_longHandled  = false;
            s_rawChangedAt = now;
        }
        return;
    }

    if (raw != s_raw)
    {
        s_raw          = raw;
        s_rawChangedAt = now;
    }

    if (raw != s_stable && now - s_rawChangedAt >= BUTTON_DEBOUNCE_MS)
    {
        s_stable = raw;

        if (s_stable == LOW)
        {
            s_pressedAt   = now;
            s_longHandled = false;
        }
        else if (!s_longHandled)
        {
            s_clickCount++;
            if (s_clickCount == 1)
                s_firstClickAt = now;
            else if (s_clickCount >= 2)
            {
                s_clickCount = 0;
                handleButtonDouble();
            }
        }
    }

    if (s_stable == LOW && !s_longHandled &&
        now - s_pressedAt >= BUTTON_SHELF_HOLD_MS)
    {
        s_longHandled = true;
        s_clickCount  = 0;
        enterShelfSleep();
    }

    if (s_clickCount == 1 && now - s_firstClickAt >= BUTTON_DOUBLE_MS)
    {
        s_clickCount = 0;
        handleButtonShort();
    }
}
