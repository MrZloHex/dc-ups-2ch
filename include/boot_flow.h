// SPDX-License-Identifier: GPL-3.0-or-later
// Pure boot-time decision logic. No globals, no side effects — every value
// this depends on is passed in. That keeps `setup()` readable and lets the
// non-trivial branches (emergency sleep vs. normal boot, portal vs. STA,
// power-up allowed vs. not) be unit-tested from the `native` env.
#pragma once

#include <stdint.h>

enum BootAction : uint8_t
{
    BOOT_NORMAL,                  // proceed with full bring-up
    BOOT_EMERGENCY_SLEEP_AGAIN,   // woke from emergency sleep, still bad -> sleep
    BOOT_EMERGENCY_SLEEP_COLD,    // cold boot with battery below sleep threshold
};

struct BootInputs
{
    bool  wokeFromEmergencySleep;
    bool  gridPresent;
    float vbatt;

    // Config subset we actually care about here.
    float battCutoff;         // LVD threshold
    float battRestore;        // hysteresis restore
    float battSleep;          // fallback deep-sleep threshold
    bool  deepSleepEnabled;
};

// Which action should setup() take, given the state right after
// initHardware() + first voltage reading?
BootAction decideBootAction(const BootInputs &in);

// Can we bring loads up on this boot? Same rule as `canPowerLoadNow()` in
// power.cpp but without the RTC-globals dependency, so it's testable.
bool canPowerLoadsOnBoot(bool gridPresent, float vbatt, float battCutoff);

// Should the emergency-sleep LVD flag be armed at boot? Cold-booting below
// the cutoff (but above the sleep threshold) means we came up on a nearly
// empty battery with the grid off — treat as if LVD had already tripped so
// the emergency-sleep timer starts.
bool shouldArmLvdAtBoot(bool gridPresent, float vbatt, float battCutoff);

// -------------------------------------------------------------------------
// Additional pure helpers used by the runtime — all here so the `native`
// test env can exercise them without pulling in Arduino / WiFi / NVS.
//
// Return values are `uint8_t` deliberately: they map 1:1 to the enum values
// in include/ups_common.h (BattState, PowerTarget). Callers cast back.
// -------------------------------------------------------------------------

// BattState: 0 = OK, 1 = WARN, 2 = CRIT. Matches `enum BattState`.
uint8_t battStateOf(float vbatt, float battCutoff, float battWarn);

// PowerTarget: 0 = NONE, 1 = ROUTER, 2 = ONT, 3 = BOTH.
// Cycle 0: whichever layer is failing (Wi-Fi ⇒ ROUTER, WAN only ⇒ ONT).
// Cycle 1+: always BOTH.
uint8_t pickRecoveryTarget(uint8_t cycleCount, bool wifiConnected);

// Is a channel allowed to be ON at this instant?
//   - grid present ⇒ always yes;
//   - grid absent, LVD not tripped ⇒ yes while V > cutoff;
//   - grid absent, LVD tripped ⇒ yes only once V >= restore (hysteresis).
bool canChannelBeOn(bool gridPresent, bool lvdTripped,
                    float vbatt, float battCutoff, float battRestore);
