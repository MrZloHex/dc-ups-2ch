// SPDX-License-Identifier: GPL-3.0-or-later
// Web UI: HTML page, /status JSON, all /… handlers.
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

extern WebServer server;
extern DNSServer dns;
extern bool      webStarted;

void beginWeb();
