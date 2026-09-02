// SPDX-License-Identifier: GPL-3.0-or-later
// WiFi STA/AP, captive portal, mDNS, reconnect logic.
#pragma once

#include <Arduino.h>
#include "ups_common.h"

extern bool     portalActive;
extern bool     wifiWasConnected;
extern bool     everWifiConnected;
extern bool     mdnsStarted;
extern String   wifiStatusText;
extern String   nextWifiConnectReason;
extern bool     manualPortalSession;

extern unsigned long portalStopAt;
extern unsigned long portalLastActivity;
extern unsigned long portalReopenAt;
extern unsigned long manualPortalUntil;
extern unsigned long wifiRetryStarted;
extern unsigned long wifiRetryWaitUntil;
extern unsigned long lastReconnect;
extern Mode           mode;
extern WifiRetryState wifiRetryState;

void touchWebActivity();

void startPortal();
void stopPortal();
bool connectSTABlocking(uint32_t timeoutMs);
void handleWifiConnected();

// Manual WiFi retry from the web panel (no ESP reboot).
bool requestPortalWifiRetry(String &answer);

// Setup AP for 15 minutes triggered by button double-tap.
void startManualPortalFromButton();

void wifiTick();
void wifiRetryTick();
void portalMaintenanceTick();
