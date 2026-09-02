// SPDX-License-Identifier: GPL-3.0-or-later
// NVS Preferences load/save for settings and recovery counters.
#pragma once

#include <Arduino.h>

void loadConfig();
void saveConfig();
void saveRecoveryCounters();
void saveNtfyCommandId(const String &id);
