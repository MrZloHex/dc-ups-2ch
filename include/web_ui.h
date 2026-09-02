// SPDX-License-Identifier: MIT
// DC-UPS-2CH — веб-интерфейс: страница, JSON /status и все /… обработчики.
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

extern WebServer server;
extern DNSServer dns;
extern bool      webStarted;

void beginWeb();
