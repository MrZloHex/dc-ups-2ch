// SPDX-License-Identifier: MIT
// DC-UPS-2CH — LED, кнопка, инициализация железа и обработка пробуждения.
#pragma once

#include <Arduino.h>
#include "esp_sleep.h"

// Флаг: кнопка ещё удерживается после EXT0-wake, не считать это новым нажатием.
extern bool buttonIgnoreUntilRelease;

void initHardware();
void handleWakeCause(esp_sleep_wakeup_cause_t cause);
void updateLeds();
void buttonTick();
