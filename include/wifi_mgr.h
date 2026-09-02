// SPDX-License-Identifier: MIT
// DC-UPS-2CH — WiFi STA/AP, captive portal, mDNS, восстановление подключения.
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
extern Mode          mode;
extern WifiRetryState wifiRetryState;

void touchWebActivity();

void startPortal();
void stopPortal();
bool connectSTABlocking(uint32_t timeoutMs);
void handleWifiConnected();

// Ручной перезапуск подключения из веб-панели (без reboot ESP).
bool requestPortalWifiRetry(String &answer);

// Setup AP на 15 минут по двойному нажатию кнопки.
void startManualPortalFromButton();

void wifiTick();
void wifiRetryTick();
void portalMaintenanceTick();
