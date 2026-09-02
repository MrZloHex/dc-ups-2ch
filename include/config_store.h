// SPDX-License-Identifier: MIT
// DC-UPS-2CH — работа с NVS Preferences: загрузка/сохранение настроек и счётчиков.
#pragma once

#include <Arduino.h>

void loadConfig();
void saveConfig();
void saveRecoveryCounters();
void saveNtfyCommandId(const String &id);
