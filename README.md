# DC-UPS-2CH — ESP32 backup power controller for network gear

> Two-channel DC UPS for a Wi-Fi router + ONT / optical terminal, built around
> an off-the-shelf 24 V AC/DC brick, an XL4016 charger, an AGM battery and two
> XL6009 buck-boost outputs. An ESP32 supervises the whole thing, exposes a
> local web panel, sends notifications through `ntfy.sh`, and even answers
> read-only status commands back through the same ntfy topic.

**Firmware version:** `2026.09.02-v6` · **License:** [GPL-3.0-or-later](LICENSE)
· **[Documentation site](https://mrzlohex.github.io/dc-ups-2ch/)**
· [Full Russian passport & manual (LaTeX)](docs/DC_UPS_documentation_v6.tex)

<p align="center">
  <img src="hardware/enclosure/renders/exploded-front.png" alt="Exploded isometric — front panel, battery, aluminium extrusion shell" width="49%"/>
  <img src="hardware/enclosure/renders/exploded-rear.png"  alt="Exploded isometric — rear panel with two-tier PCB stack (XL4016 charger + 2× XL6009 boosters + ESP32)" width="49%"/>
</p>

<p align="center"><sub>
  Exploded isometric views from the public
  <a href="https://cad.onshape.com/documents/1234567890abcdef/w/abcdef1234567890">Onshape</a>
  model. Left: front panel with IEC C14 inlet and two DC output sockets pulled off, AGM battery visible in the aluminium extrusion. Right: back panel and the two-tier power PCB assembly pulled off.
  STEP files: <a href="hardware/enclosure/step/">hardware/enclosure/step/</a>.
</sub></p>

---

## Why this exists

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

## Getting started

### Prerequisites

- ESP32 DevKit v1 (ESP-WROOM-32, USB-C or micro, CH340/CP2102 — either is fine)
- [PlatformIO Core](https://platformio.org/install/cli) (or the VS Code extension)
- The rest of the hardware from [`docs/HARDWARE.md`](docs/HARDWARE.md)

### Build & flash

```bash
git clone https://github.com/YOUR_USER/dc-ups-2ch.git
cd dc-ups-2ch

# Everything at once (firmware + PDFs), stamped with version + git hash + date:
make                   # or: make all
make upload            # firmware + flash
make monitor           # 115200 baud

# Or use PlatformIO directly — the pre-script still stamps the build:
pio run
pio run -t upload
pio device monitor
```

The version stamped into the firmware and both PDFs comes from the top-level
`VERSION` file plus `git rev-parse --short HEAD` and today's UTC date. It
shows up in the serial boot log, in the web panel header, in ntfy status
replies, and on the printable device label. Override any of them:

```bash
make VERSION=2026.10.01-v7            # bump semantic version for a release build
make DOCREV="Rev. 3"                  # override the document revision label
```

`make version` prints exactly what will be stamped, `make help` lists every
target.

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

### Button reference

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

## Documentation

- **[Documentation site](https://mrzlohex.github.io/dc-ups-2ch/)** — GitHub
  Pages landing page linking to every PDF. This URL is also encoded in the QR
  code printed on the enclosure label — scan the label with any phone to jump
  straight here.
- [`docs/index.html`](docs/index.html) — source of the site above (served from
  the `docs/` folder on the `master` branch)
- [`docs/HARDWARE.md`](docs/HARDWARE.md) — BOM, GPIO map, ASCII wiring, calibration
- [`hardware/enclosure/`](hardware/enclosure/) — 3D-printable case (Onshape
  source, STEP exports, CC-BY-SA 4.0) with ready-to-paste
  [Printables](hardware/enclosure/PRINTABLES.md) and
  [MakerWorld](hardware/enclosure/MAKERWORLD.md) listing templates
- [`docs/DC_UPS_documentation_v6.tex`](docs/DC_UPS_documentation_v6.tex) — full
  Russian passport, service manual, warranty template
- [`docs/DC_UPS_user_manual_v6.tex`](docs/DC_UPS_user_manual_v6.tex) — short
  A5 user manual (8 pages, Russian) with local-panel + docs-site QR codes
- [`docs/DC_UPS_label_v6.tex`](docs/DC_UPS_label_v6.tex) — 90 × 55 mm enclosure
  label; QR code encodes the documentation-site URL above
- [`Makefile`](Makefile) — top-level orchestrator: `make` (firmware + docs),
  `make upload` (flash), `make version` (print stamped identity)
- [`docs/Makefile`](docs/Makefile) — build the PDFs with `make -C docs all`
  (needs `xelatex` + `latexmk` and the DejaVu fonts); `make -C docs clean`
  wipes every intermediate
- [`scripts/inject_version.py`](scripts/inject_version.py) — PlatformIO
  pre-script that stamps `FW_VERSION` / `FW_GIT_HASH` / `FW_BUILD_DATE`
  into the firmware; runs automatically on every `pio run`
- [`CHANGELOG.md`](CHANGELOG.md) — release notes

## Contributing

Issues and PRs welcome. Please:

- keep the firmware buildable with the pinned `espressif32@^6.9.0` platform
  (the watchdog and RTC-GPIO APIs used here are frozen on that major);
- avoid new external Arduino libraries — everything so far fits in the core;
- match the module split described above rather than dumping new features
  into `main.cpp`.

## License

**Firmware & documentation:** GNU General Public License v3.0 or later —
see [LICENSE](LICENSE). If you build a product on top of this firmware,
you must make your source available under the same license.

**Enclosure / 3D model:** Creative Commons Attribution-ShareAlike 4.0
International (CC-BY-SA 4.0) — see
[`hardware/enclosure/LICENSE-CC-BY-SA-4.0.txt`](hardware/enclosure/LICENSE-CC-BY-SA-4.0.txt).

Hardware wiring is documented for personal / educational use; recycled
AC/DC bricks and Chinese DC/DC modules used in the BOM come with their
own certifications (or don't).
