// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the pure boot / recovery decision logic in boot_flow.cpp.
// Runs under the `native` env — no ESP32 / Arduino / WiFi involved.
//
//   pio test -e native
//
// Enum value assumptions match ups_common.h:
//   BattState:   0=OK, 1=WARN, 2=CRIT
//   PowerTarget: 0=NONE, 1=ROUTER, 2=ONT, 3=BOTH

#include <unity.h>
#include "boot_flow.h"

// ---------------------------------------------------------------------------
// decideBootAction — cold boot vs. wake vs. emergency sleep
// ---------------------------------------------------------------------------

static BootInputs makeIn(bool wake, bool grid, float vb)
{
    // Typical config defaults from config_store.cpp.
    return BootInputs{ wake, grid, vb,
                       /*battCutoff*/  11.0f,
                       /*battRestore*/ 12.5f,
                       /*battSleep*/   10.8f,
                       /*deepSleep*/   true };
}

void test_cold_boot_grid_ok(void)
{
    // Grid up, battery healthy -> normal boot regardless of thresholds.
    BootInputs in = makeIn(false, true, 12.8f);
    TEST_ASSERT_EQUAL(BOOT_NORMAL, decideBootAction(in));
}

void test_cold_boot_no_grid_dead_battery(void)
{
    // No grid + battery below sleep threshold + deep sleep enabled -> refuse.
    BootInputs in = makeIn(false, false, 10.5f);
    TEST_ASSERT_EQUAL(BOOT_EMERGENCY_SLEEP_COLD, decideBootAction(in));
}

void test_cold_boot_no_grid_deep_sleep_disabled_stays_up(void)
{
    // Same low battery but deep sleep disabled -> user asked us to stay up.
    BootInputs in = makeIn(false, false, 10.5f);
    in.deepSleepEnabled = false;
    TEST_ASSERT_EQUAL(BOOT_NORMAL, decideBootAction(in));
}

void test_cold_boot_no_grid_battery_above_sleep(void)
{
    // Grid off, but battery still above sleep threshold -> proceed normally.
    // The regular protectTick() will handle LVD when it drops further.
    BootInputs in = makeIn(false, false, 11.7f);
    TEST_ASSERT_EQUAL(BOOT_NORMAL, decideBootAction(in));
}

void test_wake_grid_back_forces_normal(void)
{
    // Wake from emergency sleep with the grid restored -> proceed to full
    // bring-up even if the battery is still below restore hysteresis.
    BootInputs in = makeIn(true, true, 11.4f);
    TEST_ASSERT_EQUAL(BOOT_NORMAL, decideBootAction(in));
}

void test_wake_no_grid_battery_below_restore_sleeps_again(void)
{
    // Wake, still no grid, battery below the restore hysteresis -> sleep again.
    BootInputs in = makeIn(true, false, 12.4f);
    TEST_ASSERT_EQUAL(BOOT_EMERGENCY_SLEEP_AGAIN, decideBootAction(in));
}

void test_wake_no_grid_battery_at_restore_proceeds(void)
{
    // Battery recovered to the restore threshold -> bring the system back up.
    BootInputs in = makeIn(true, false, 12.5f);
    TEST_ASSERT_EQUAL(BOOT_NORMAL, decideBootAction(in));
}

// ---------------------------------------------------------------------------
// canPowerLoadsOnBoot / shouldArmLvdAtBoot
// ---------------------------------------------------------------------------

void test_can_power_loads_on_boot_variants(void)
{
    TEST_ASSERT_TRUE (canPowerLoadsOnBoot(true,  10.0f, 11.0f)); // grid rescues
    TEST_ASSERT_TRUE (canPowerLoadsOnBoot(false, 12.0f, 11.0f)); // above cutoff
    TEST_ASSERT_FALSE(canPowerLoadsOnBoot(false, 11.0f, 11.0f)); // at cutoff = block
    TEST_ASSERT_FALSE(canPowerLoadsOnBoot(false, 10.5f, 11.0f)); // below cutoff
}

void test_should_arm_lvd_at_boot(void)
{
    TEST_ASSERT_TRUE (shouldArmLvdAtBoot(false, 11.0f, 11.0f)); // at cutoff, no grid
    TEST_ASSERT_TRUE (shouldArmLvdAtBoot(false, 10.5f, 11.0f)); // below, no grid
    TEST_ASSERT_FALSE(shouldArmLvdAtBoot(true,  10.5f, 11.0f)); // grid up -> no arm
    TEST_ASSERT_FALSE(shouldArmLvdAtBoot(false, 11.5f, 11.0f)); // healthy
}

// ---------------------------------------------------------------------------
// battStateOf — 3-way LVD/WARN/OK boundaries
// ---------------------------------------------------------------------------

void test_batt_state_of_boundaries(void)
{
    // cutoff=11.0, warn=11.5
    TEST_ASSERT_EQUAL_UINT8(2, battStateOf(10.9f, 11.0f, 11.5f)); // CRIT
    TEST_ASSERT_EQUAL_UINT8(2, battStateOf(11.0f, 11.0f, 11.5f)); // at cutoff = CRIT
    TEST_ASSERT_EQUAL_UINT8(1, battStateOf(11.1f, 11.0f, 11.5f)); // WARN band
    TEST_ASSERT_EQUAL_UINT8(1, battStateOf(11.5f, 11.0f, 11.5f)); // at warn = WARN
    TEST_ASSERT_EQUAL_UINT8(0, battStateOf(11.6f, 11.0f, 11.5f)); // OK
    TEST_ASSERT_EQUAL_UINT8(0, battStateOf(13.8f, 11.0f, 11.5f)); // OK (charging)
}

// ---------------------------------------------------------------------------
// pickRecoveryTarget — first-cycle failing-layer, then both
// ---------------------------------------------------------------------------

void test_pick_recovery_target_first_cycle(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, pickRecoveryTarget(0, /*wifi*/ false)); // ROUTER
    TEST_ASSERT_EQUAL_UINT8(2, pickRecoveryTarget(0, /*wifi*/ true));  // ONT
}

void test_pick_recovery_target_second_cycle_is_both(void)
{
    TEST_ASSERT_EQUAL_UINT8(3, pickRecoveryTarget(1, false));
    TEST_ASSERT_EQUAL_UINT8(3, pickRecoveryTarget(1, true));
    TEST_ASSERT_EQUAL_UINT8(3, pickRecoveryTarget(2, false));
    TEST_ASSERT_EQUAL_UINT8(3, pickRecoveryTarget(9, true));
}

// ---------------------------------------------------------------------------
// canChannelBeOn — hysteresis around LVD
// ---------------------------------------------------------------------------

void test_can_channel_be_on_grid_always_wins(void)
{
    TEST_ASSERT_TRUE(canChannelBeOn(true,  true,  10.0f, 11.0f, 12.5f));
    TEST_ASSERT_TRUE(canChannelBeOn(true,  false, 10.0f, 11.0f, 12.5f));
}

void test_can_channel_be_on_no_lvd_uses_cutoff(void)
{
    // Not tripped: threshold is > cutoff (strict).
    TEST_ASSERT_FALSE(canChannelBeOn(false, false, 11.0f, 11.0f, 12.5f));
    TEST_ASSERT_TRUE (canChannelBeOn(false, false, 11.1f, 11.0f, 12.5f));
}

void test_can_channel_be_on_lvd_tripped_requires_restore(void)
{
    // Tripped: threshold jumps to restore (>= is enough).
    TEST_ASSERT_FALSE(canChannelBeOn(false, true, 12.4f, 11.0f, 12.5f));
    TEST_ASSERT_TRUE (canChannelBeOn(false, true, 12.5f, 11.0f, 12.5f));
    TEST_ASSERT_TRUE (canChannelBeOn(false, true, 12.9f, 11.0f, 12.5f));
}

// ---------------------------------------------------------------------------
// Unity plumbing
// ---------------------------------------------------------------------------

void setUp(void)    {}
void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_cold_boot_grid_ok);
    RUN_TEST(test_cold_boot_no_grid_dead_battery);
    RUN_TEST(test_cold_boot_no_grid_deep_sleep_disabled_stays_up);
    RUN_TEST(test_cold_boot_no_grid_battery_above_sleep);
    RUN_TEST(test_wake_grid_back_forces_normal);
    RUN_TEST(test_wake_no_grid_battery_below_restore_sleeps_again);
    RUN_TEST(test_wake_no_grid_battery_at_restore_proceeds);

    RUN_TEST(test_can_power_loads_on_boot_variants);
    RUN_TEST(test_should_arm_lvd_at_boot);

    RUN_TEST(test_batt_state_of_boundaries);

    RUN_TEST(test_pick_recovery_target_first_cycle);
    RUN_TEST(test_pick_recovery_target_second_cycle_is_both);

    RUN_TEST(test_can_channel_be_on_grid_always_wins);
    RUN_TEST(test_can_channel_be_on_no_lvd_uses_cutoff);
    RUN_TEST(test_can_channel_be_on_lvd_tripped_requires_restore);

    return UNITY_END();
}
