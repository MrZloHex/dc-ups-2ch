// SPDX-License-Identifier: GPL-3.0-or-later
//
// Build-time firmware identity. Normally injected by scripts/inject_version.py
// (a PlatformIO pre-script) or by the root Makefile. Fallbacks below let the
// firmware compile from the Arduino IDE without the script.
//
//   FW_VERSION    — semantic version from the top-level VERSION file
//   FW_GIT_HASH   — short commit hash, "-dirty" if working tree changed
//   FW_BUILD_DATE — UTC build date, YYYY-MM-DD
#pragma once

#ifndef FW_VERSION
#define FW_VERSION    "dev"
#endif

#ifndef FW_GIT_HASH
#define FW_GIT_HASH   "unknown"
#endif

#ifndef FW_BUILD_DATE
#define FW_BUILD_DATE "unknown"
#endif

// e.g. "2026.09.02-v6 (abc1234, 2026-09-02)"
#define FW_FULL_ID    FW_VERSION " (" FW_GIT_HASH ", " FW_BUILD_DATE ")"
