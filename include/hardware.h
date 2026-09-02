// SPDX-License-Identifier: GPL-3.0-or-later
// LEDs, button state machine, wake-cause dispatch.
#pragma once

#include <Arduino.h>
#include "esp_sleep.h"

// True while the wake button is still held after an EXT0 wake — must ignore
// it until released so it isn't counted as a new press.
extern bool buttonIgnoreUntilRelease;

void initHardware();
void handleWakeCause(esp_sleep_wakeup_cause_t cause);
void updateLeds();
void buttonTick();
