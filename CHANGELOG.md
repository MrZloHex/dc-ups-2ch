# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and this project uses date-based firmware versions (`YYYY.MM.DD-vN`).

## [Unreleased]

### Added
- Top-level `Makefile` orchestrator: `make` (firmware + PDFs), `make upload`,
  `make monitor`, `make flash`, `make version`, `make clean`, `make distclean`.
- `VERSION` file as the single source of truth for the semantic version.
- `scripts/inject_version.py` — PlatformIO pre-script that stamps `FW_VERSION`,
  `FW_GIT_HASH` (with `-dirty` suffix) and `FW_BUILD_DATE` into the firmware
  as `-D` macros. `include/version.h` provides safe fallbacks so the code
  still compiles from any IDE.
- `docs/Makefile` regenerates `docs/build_info.tex` on every run and stamps
  the same identity into the LaTeX passport and enclosure label. The `.tex`
  sources now use `\fwversion` / `\hwversion` / `\builddate` / `\buildcommit` /
  `\docrev` commands with `\IfFileExists` fallbacks so they compile standalone.
- `hardware/enclosure/` folder with Onshape-exported STEP files, CC-BY-SA 4.0
  license, and copy/paste listing templates for Printables and MakerWorld.
- Serial boot log and web-panel header now show version + git hash + build date.

### Changed
- **License:** firmware and repository documentation moved from MIT to
  **GNU GPL-3.0-or-later**. Every source file now carries an
  `SPDX-License-Identifier: GPL-3.0-or-later` header; the enclosure /
  3D model stays under CC-BY-SA 4.0.
- Split the 2600-line `main.cpp` into focused modules under `src/` and
  `include/` (`ups_common`, `config_store`, `event_log`, `hardware`, `power`,
  `wifi_mgr`, `ntfy`, `sleep_modes`, `recovery`, `web_ui`). No behavioural
  changes.
- Added English + Russian `README`s and `docs/HARDWARE.md` (BOM, GPIO map,
  ASCII wiring, calibration procedure).
- `.gitignore` expanded to cover every LaTeX intermediate (`.xdv`, glossary /
  index bits, beamer bits, minted) plus the generated `docs/build_info.tex`.

## [2026.09.02-v6] — 2026-09-02

Baseline release matching the Russian passport (Rev. 2, 2026-09-02).

### Firmware
- Two independent DC outputs (`ROUTER`, `ONT`) with per-channel manual
  `ON`/`OFF`/`AUTO` and remote restart.
- LVD with `battCutoff` / `battRestore` hysteresis, `battWarn` early warning,
  `battSleep` fallback low-voltage sleep threshold.
- Grid detection with configurable `gridOn` / `gridOff` hysteresis and
  debounce (default 3 s).
- Emergency deep sleep after LVD (default: probe grid every 30 s).
- Shelf Sleep: 10 s button hold, wake only by another press.
- Automatic Wi-Fi / internet recovery: probe → restart the right channel →
  restart both → optional single ESP reboot; counters reset after 10 min of
  stable connectivity.
- `ntfy.sh` outgoing notifications for outages, LVD, boot, sleep transitions,
  recovery actions.
- Inbound read-only ntfy commands: `!ups`, `!ups status`, `!ups ping`.
- Web panel with mDNS (`http://dc-ups.local/`), captive setup portal
  (`DC-UPS-Setup`), optional HTTP-Basic protection, per-channel calibration.
- RAM-only event log (60 entries), status JSON on `/status`, live UI polling.

### Hardware
- Baseline BOM: ESP32 DevKit v1, XL4016 charger, AGM 12 V 7 Ah, 2× XL6009
  outputs, 2× IRF9Zxx-class P-MOSFETs driven by 2N2222 NPN, 100k/10k dividers
  on `GPIO34`/`GPIO35`, tact button on `GPIO14`.
