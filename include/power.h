// SPDX-License-Identifier: GPL-3.0-or-later
// Load control, LVD, protection tick, power-cycle scheduler.
#pragma once

#include <Arduino.h>
#include "ups_common.h"

extern bool          routerOn, ontOn;
extern bool          gridPresent, gridCandidate;
extern bool          warnSent;
extern bool          lvdTripped;
extern LoadMode      routerMode, ontMode;
extern BattState     battState;
extern float         lastVbatt, lastVgrid;
extern unsigned long gridChangeSince;
extern unsigned long routerTurnedOnAt, ontTurnedOnAt;
extern unsigned long outageStartedAt;
extern unsigned long lastOutageDurationMs;
extern uint32_t      outageCount;

extern PowerCycleState powerCycleState;
extern PowerTarget     powerCycleTarget;
extern unsigned long   powerCycleAt;
extern bool            powerCycleAutomatic;
extern String          powerCycleReason;

extern unsigned long rebootAt;

void setRouter(bool on);
void setOnt(bool on);
void setBothLoads(bool on);
void setTargetPower(PowerTarget target, bool on);

bool anyLoadOn();
bool networkLoadsOn();
bool canPowerLoadNow();
unsigned long equipmentTurnedOnAt();

BattState getBattState(float vb);

bool requestRouterRestart(String &answer);
bool requestOntRestart(String &answer);
bool requestBothRestart(String &answer);
bool requestTargetRestart(PowerTarget target, String &answer,
                          bool automatic = false,
                          const String &reason = "manual restart");

void protectTick();
void powerCycleTick();
void rebootTick();
