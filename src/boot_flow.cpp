// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot_flow.h"

BootAction decideBootAction(const BootInputs &in)
{
    if (in.wokeFromEmergencySleep)
    {
        // We already scheduled the periodic wake-up. If neither the grid
        // came back nor the battery recovered above the restore hysteresis,
        // just measure again next tick — don't burn 20+ s on WiFi.
        if (!in.gridPresent && in.vbatt < in.battRestore)
            return BOOT_EMERGENCY_SLEEP_AGAIN;
        return BOOT_NORMAL;
    }

    // Cold boot on a mostly-dead battery with no mains: refuse to bring
    // WiFi + loads up; go straight to the periodic-probe sleep loop.
    if (in.deepSleepEnabled && !in.gridPresent && in.vbatt <= in.battSleep)
        return BOOT_EMERGENCY_SLEEP_COLD;

    return BOOT_NORMAL;
}

bool canPowerLoadsOnBoot(bool gridPresent, float vbatt, float battCutoff)
{
    return gridPresent || (vbatt > battCutoff);
}

bool shouldArmLvdAtBoot(bool gridPresent, float vbatt, float battCutoff)
{
    return !gridPresent && vbatt <= battCutoff;
}

uint8_t battStateOf(float vbatt, float battCutoff, float battWarn)
{
    // Mirrors getBattState() in power.cpp but takes thresholds as inputs.
    if (vbatt <= battCutoff) return 2;  // BATT_CRIT
    if (vbatt <= battWarn)   return 1;  // BATT_WARN
    return 0;                            // BATT_OK
}

uint8_t pickRecoveryTarget(uint8_t cycleCount, bool wifiConnected)
{
    if (cycleCount == 0)
        return wifiConnected ? 2 : 1;  // TARGET_ONT : TARGET_ROUTER
    return 3;                          // TARGET_BOTH
}

bool canChannelBeOn(bool gridPresent, bool lvdTripped,
                    float vbatt, float battCutoff, float battRestore)
{
    if (gridPresent) return true;
    return lvdTripped ? (vbatt >= battRestore) : (vbatt > battCutoff);
}
