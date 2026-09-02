// SPDX-License-Identifier: GPL-3.0-or-later
// Outgoing ntfy notifications and read-only inbound commands (!ups status/ping).
#pragma once

#include <Arduino.h>

extern bool          ntfyIpPending;
extern String        ntfyIpReason;
extern unsigned long ntfyNextTry;
extern unsigned long lastNtfyCommandPoll;
extern String        ntfyLastSeenId;
extern String        ntfyLastCommandId;

bool   sendNtfy(const String &text);
String buildStatusMessage(const String &headline);
void   queueIpNotification(const String &reason);

void   ntfyTick();
void   ntfyCommandTick();
