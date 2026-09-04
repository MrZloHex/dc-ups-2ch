// SPDX-License-Identifier: GPL-3.0-or-later
// LEDs, button state machine, wake-cause dispatch.
#pragma once

#include <Arduino.h>
#include "esp_sleep.h"

// True while the wake button is still held after an EXT0 wake — must ignore
// it until released so it isn't counted as a new press.
extern bool buttonIgnoreUntilRelease;

// High-level LED mode. updateLeds() derives the actual GPIO state from this
// plus gridPresent / battState / anyLoadOn(). State-transition points
// (connectSTABlocking, handleWifiConnected, startPortal, stopPortal, ...)
// just set this — no direct digitalWrite() on the LED pins from elsewhere.
enum LedMode {
    LED_MODE_RUN,        // steady green if grid; red per battery/grid rules
    LED_MODE_BOOT_WIFI,  // ~2 Hz green blink, red off — booting / joining WiFi
    LED_MODE_PORTAL,     // slow alternating green/red — setup AP active
};
extern LedMode ledMode;

void initHardware();
void handleWakeCause(esp_sleep_wakeup_cause_t cause);
void updateLeds();
void buttonTick();
