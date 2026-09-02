// SPDX-License-Identifier: MIT
// DC-UPS-2CH — управление нагрузками, LVD, protectTick и power-cycle.
#pragma once

#include <Arduino.h>
#include "ups_common.h"

// Наблюдаемое состояние силовой части
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

// Планировщик перезапусков
extern PowerCycleState powerCycleState;
extern PowerTarget     powerCycleTarget;
extern unsigned long   powerCycleAt;
extern bool            powerCycleAutomatic;
extern String          powerCycleReason;

// Планировщик перезагрузки самой ESP
extern unsigned long rebootAt;

// --- Управление ---
void setRouter(bool on);
void setOnt(bool on);
void setBothLoads(bool on);
void setTargetPower(PowerTarget target, bool on);

bool anyLoadOn();
bool networkLoadsOn();
bool canPowerLoadNow();
unsigned long equipmentTurnedOnAt();

BattState getBattState(float vb);

// --- Пользовательские действия ---
bool requestRouterRestart(String &answer);
bool requestOntRestart(String &answer);
bool requestBothRestart(String &answer);
bool requestTargetRestart(PowerTarget target, String &answer,
                          bool automatic = false,
                          const String &reason = "ручной перезапуск");

// --- Тики главного цикла ---
void protectTick();
void powerCycleTick();
void rebootTick();
