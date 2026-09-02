# DC-UPS-2CH — ESP32 backup power controller for network gear

> Two-channel DC UPS for a Wi-Fi router + ONT / optical terminal, built around
> an off-the-shelf 24 V AC/DC brick, an XL4016 charger, an AGM battery and two
> XL6009 buck-boost outputs. An ESP32 supervises the whole thing, exposes a
> local web panel, sends notifications through `ntfy.sh`, and even answers
> read-only status commands back through the same ntfy topic.

**Firmware version:** `2026.09.02-v6` · **License:** [MIT](LICENSE)
· [Русская версия README](README.ru.md)
· [Full Russian passport & manual (LaTeX)](docs/DC_UPS_documentation_v6.tex)

---

## Why this exists

A rural Wi-Fi router + fibre ONT combo needs to stay online during frequent short
mains outages. Off-the-shelf DC-UPS boards do the raw switching but give you
zero visibility: no notification when the mains dies, no way to power-cycle
just the ONT when your provider's WAN glitches, no low-battery protection you
can actually tune.

This project keeps the hardware boring (all modules you can buy on any
marketplace) and puts the intelligence in the ESP32 firmware:

- automatic mains ↔ battery switching (via the shared 13.8 V bus, no relay);
- true low-voltage disconnect with configurable thresholds and hysteresis;
- **two independent DC outputs** (ROUTER, ONT) with per-channel manual override
  and remote restart;
- automatic Wi-Fi / internet health check with staged recovery (restart ONT →
  restart router → restart both → optionally reboot the ESP);
- push notifications via [ntfy.sh](https://ntfy.sh/) — outages, LVD, boots,
  auto-recovery actions;
- inbound read-only commands over ntfy: `!ups`, `!ups status`, `!ups ping`;
- self-hosted web panel with mDNS (`http://dc-ups.local/`);
- captive-portal setup AP (`DC-UPS-Setup`) when Wi-Fi isn't configured or dies;
- two deep-sleep modes: emergency (timer wake, keeps checking mains) and
  shelf-sleep (button wake only, for storage/transport).

## Hardware overview

```
230 V AC ─▶ AC/DC 24 V ─▶ XL4016 CC/CV ─▶ 13.8 V bus ◀─▶ AGM 12 V 7 Ah
                                              │
                          ┌───────────────────┼───────────────────┐
                          ▼                                       ▼
                  P-MOSFET + XL6009                       P-MOSFET + XL6009
                          │                                       │
                          ▼                                       ▼
                        ROUTER                                  ONT / AUX
                          ▲                                       ▲
                          │                                       │
                          └─── GPIO32 ─── ESP32 ─── GPIO26 ───────┘
                                            │
                                            ▼
                                    ADC ◀── 24 V bus & battery
                                            │
                                    Button ─┘ (GPIO14, EXT0 wake)
```

Full wiring, GPIO map, calibration and BOM: [`docs/HARDWARE.md`](docs/HARDWARE.md).

## Firmware architecture

The firmware is intentionally free of external libraries — everything ships in
Arduino-ESP32 core. Code is split into small modules that all share global
state through `include/ups_common.h`:

| Module           | Responsibility                                                         |
|------------------|------------------------------------------------------------------------|
| `main.cpp`       | `setup()` / `loop()` orchestration only                                |
| `ups_common.*`   | pins, timing constants, enums, `Config` struct, common utilities       |
| `config_store.*` | NVS `Preferences` load/save + validation                               |
| `hardware.*`     | LED patterns, button state machine, wake-cause handling                |
| `power.*`        | load control, LVD, per-channel modes, power-cycle scheduler            |
| `wifi_mgr.*`     | STA/AP, captive portal, mDNS, manual & automatic reconnect             |
| `ntfy.*`         | outgoing pushes + inbound command polling (no HTTPS libs beyond core)  |
| `sleep_modes.*`  | emergency deep-sleep + shelf-sleep, RTC-memory bookkeeping             |
| `recovery.*`     | internet health probes (TCP to 1.1.1.1:443 / 8.8.8.8:53), staged fixes |
| `web_ui.*`       | `WebServer` handlers + inline HTML/CSS/JS panel                        |
| `event_log.*`    | in-RAM ring-buffer log                                                 |

## Getting started

### Prerequisites

- ESP32 DevKit v1 (ESP-WROOM-32, USB-C or micro, CH340/CP2102 — either is fine)
- [PlatformIO Core](https://platformio.org/install/cli) (or the VS Code extension)
- The rest of the hardware from [`docs/HARDWARE.md`](docs/HARDWARE.md)

### Build & flash

```bash
git clone https://github.com/YOUR_USER/dc-ups-2ch.git
cd dc-ups-2ch
pio run                # build
pio run -t upload      # flash
pio device monitor     # 115200 baud
```

### First boot

1. On first boot (or if the saved Wi-Fi credentials fail) the device spins up
   an open access point named **`DC-UPS-Setup`** with password
   `dc-ups-setup`.
2. Connect any phone/laptop, browse to `http://192.168.4.1/`, fill in your
   Wi-Fi SSID/password, your `ntfy.sh` topic (**pick a long, unguessable
   string** — the topic is effectively a secret URL), and hit save. The device
   reboots.
3. From now on the panel lives at `http://dc-ups.local/` (mDNS) or the IP the
   ESP32 pushes to your ntfy topic on every successful connect.

### Button reference (GPIO14)

| Gesture               | Action                                                          |
|-----------------------|-----------------------------------------------------------------|
| 1× short press        | send full status message to ntfy (2× green blink = ok)          |
| 2× short press        | open setup AP for 15 minutes (both LEDs blink once)             |
| Hold 10 s             | Shelf Sleep — everything off, wake only by another press        |
| Any press in shelf    | boot the device again                                           |

### ntfy commands

Send any of these to your topic (e.g. from the ntfy app or `curl`):

```
!ups          # or "!ups status" — reply with full status
!ups ping     # reply "PONG ..." + full status
```

The device polls the topic ~every 15 s while online. There are **no
inbound control commands** on purpose: the topic is only a read-only status
channel.

## Safety

The enclosure contains live 230 V AC. Do not open with mains connected and
without disconnecting the battery — SLA batteries can deliver hundreds of amps
into a short. See [`docs/HARDWARE.md`](docs/HARDWARE.md#safety) and the
Russian manual for full teardown / battery replacement procedure.

## Documentation

- [`docs/HARDWARE.md`](docs/HARDWARE.md) — BOM, GPIO map, ASCII wiring, calibration
- [`hardware/enclosure/`](hardware/enclosure/) — 3D-printable case (Onshape
  source, STEP exports, CC-BY-SA 4.0) with ready-to-paste
  [Printables](hardware/enclosure/PRINTABLES.md) and
  [MakerWorld](hardware/enclosure/MAKERWORLD.md) listing templates
- [`docs/DC_UPS_documentation_v6.tex`](docs/DC_UPS_documentation_v6.tex) — full
  Russian passport, service manual, warranty template
- [`docs/DC_UPS_label_v6.tex`](docs/DC_UPS_label_v6.tex) — 90 × 55 mm enclosure
  label with QR to the panel
- [`docs/Makefile`](docs/Makefile) — build the PDFs with `make -C docs all`
  (needs `xelatex` + `latexmk` and the DejaVu fonts); `make -C docs clean`
  wipes every intermediate
- [`CHANGELOG.md`](CHANGELOG.md) — release notes

## Contributing

Issues and PRs welcome. Please:

- keep the firmware buildable with the pinned `espressif32@^6.9.0` platform
  (the watchdog and RTC-GPIO APIs used here are frozen on that major);
- avoid new external Arduino libraries — everything so far fits in the core;
- match the module split described above rather than dumping new features
  into `main.cpp`.

## License

MIT — see [LICENSE](LICENSE). Hardware wiring is documented for personal /
educational use; recycled AC/DC bricks and Chinese DC/DC modules used in the
BOM come with their own certifications (or don't).
