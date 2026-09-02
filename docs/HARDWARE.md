# DC-UPS-2CH — hardware notes

> This is a "buy the modules, don't design a PCB" project. Everything is
> wired point-to-point (or on a small perfboard) so anyone with a soldering
> iron can reproduce it. The full Russian passport (`DC_UPS_documentation_v6.tex`)
> has the same information plus service checklists, warranty template and
> service diagrams — this file is the English quick reference.

## Bill of materials

| # | Part                                       | Notes                                                            |
|---|--------------------------------------------|------------------------------------------------------------------|
| 1 | ESP32 DevKit v1 (ESP-WROOM-32)             | USB-C or micro; CH340/CP2102 both fine                           |
| 2 | AC/DC brick 230 V → 24 V, ≥ 60 W           | any reputable enclosed switcher                                  |
| 3 | XL4016 CC/CV step-down module              | set to **13.8 V** open-circuit, current-limit ≈ **1.5 A**        |
| 4 | AGM/SLA battery 12 V, 7 Ah                 | any brand that fits your enclosure; keep the terminal type same  |
| 5 | 2 × XL6009 buck-boost                      | one per output; adjust to the actual load voltage before wiring  |
| 6 | 2 × P-MOSFET, e.g. IRF9Z34N                | high-side switch; needs to handle your peak output current       |
| 7 | 2 × 2N2222 NPN                             | gate driver from a 3.3 V GPIO to a P-MOSFET gate                 |
| 8 | 2 × 10 kΩ gate pull-up (Gate ↔ Source)     | keeps the P-MOSFET fully off when the NPN is off                 |
| 9 | 2 × 100 kΩ + 2 × 10 kΩ                     | voltage dividers for `V_BATT` and `V_24V` sensing                |
|10 | 1 × 5 A automotive fuse + holder           | mount at the battery `+` terminal, as close as possible          |
|11 | Reverse-blocking / OR-ing diode (optional) | if your specific arrangement needs it — depends on charger topo  |
|12 | 1 × 6 × 6 mm tact switch                   | GPIO14 to GND (uses internal pull-up)                            |
|13 | 2 × LED (green + red) + series resistors   | ~330 Ω is fine for 3.3 V drive                                   |
|14 | Screw terminals / DC barrel jacks          | pick to match your router's / ONT's input plugs                  |

Anything not on this list (heatsinks, standoffs, wire, enclosure) is up to you.

## GPIO map

| Pin        | Direction | Function                                              |
|------------|-----------|-------------------------------------------------------|
| `GPIO35`   | ADC1_IN7  | `V_BATT` sense (100 kΩ / 10 kΩ divider → x11)         |
| `GPIO34`   | ADC1_IN6  | `V_24V` sense (100 kΩ / 10 kΩ divider → x11)          |
| `GPIO32`   | OUT       | `ROUTER` channel: NPN base → P-MOSFET gate            |
| `GPIO25`   | OUT       | `ONT` channel: NPN base → P-MOSFET gate               |
| `GPIO14`   | IN_PULLUP | service button to GND; **RTC-GPIO → EXT0 wake source**|
| `GPIO13`   | OUT       | green LED: mains present                              |
| `GPIO12`   | OUT       | red LED: battery / warning / fault                    |

> **ADC1 only.** Voltage sensing must stay on GPIO32-39. ADC2 is unusable
> while Wi-Fi is on.

> **GPIO14 is a specific choice.** It's an RTC-GPIO on the classic ESP32,
> which is exactly what's needed to wake from Shelf Sleep via EXT0. Do not
> reassign to a non-RTC pin.

## Voltage divider math

```
V_pin = V_source · R_bottom / (R_top + R_bottom)
V_pin = V_source · 10k / (100k + 10k) = V_source / 11

At V_source = 15.0 V  ->  V_pin ≈ 1.36 V  (well inside 0..3.3 V ADC range)
At V_source = 13.8 V  ->  V_pin ≈ 1.25 V
At V_source = 24.0 V  ->  V_pin ≈ 2.18 V
```

The firmware assumes `DIV = 11.0` for both channels. If you use different
resistors, edit `DIV_VBATT` / `DIV_VGRID` in
[`include/ups_common.h`](../include/ups_common.h). Fine per-device correction
(±3 %) is done at runtime through the web panel using a multimeter — no code
change needed.

## ASCII wiring (one channel — mirror for the second)

```
              +Vbus 13.8 V (charger + battery via fuse)
                          │
                          │
                     ┌────┴─────┐    Gate 10 kΩ pull-up
                     │  P-MOSFET │ ◀──────────┐
                     │  (S=Vbus) │            │
                     └────┬─────┘             │
                          │                   │
                     Drain│                   │
                          ▼                   │
                    XL6009 IN                 │
                          │                   │
                    XL6009 OUT ─▶ ROUTER / ONT│
                                              │
                                              │
                                   ┌──────────┴──┐
                                   │   2N2222     │  (BJT open when GPIO HIGH)
                                   │  base ← 1kΩ │
                                   │              │
                          GPIO32 ─▶│ base         │
                    (or  GPIO25)   │ collector ──►│ P-MOSFET Gate
                                   │ emitter ─── GND
                                   └──────────────┘
```

Logic:

- GPIO **HIGH** → NPN saturates → pulls the P-MOSFET gate to GND
  → **P-MOSFET conducts → load ON**.
- GPIO **LOW** → NPN off → gate is held at Vbus by the 10 kΩ pull-up
  → **P-MOSFET off → load OFF**.

## Charger settings (XL4016)

1. Disconnect everything downstream.
2. Feed the XL4016 from the 24 V brick.
3. **Voltage pot:** turn the CV trim until the open-circuit output at the
   pads that will feed the battery reads exactly **13.8 V** at the battery
   terminals. Compensate for wire drop by measuring at the battery, not at
   the module.
4. **Current pot:** short the output through an ammeter for a brief pulse
   (or use a resistive dummy load) and set the CC limit to **≈ 1.5 A**.
5. Only then connect the battery.

## Setting the XL6009 outputs

1. Look up the required voltage of the load (router: usually 12 V, ONT:
   often 12 V, sometimes 9 V). Verify with a multimeter on the *original*
   PSU under no load.
2. Feed each XL6009 from the 13.8 V bus (through a temporary lead — do
   **not** connect the load yet).
3. Turn the output pot until the measured voltage matches the target
   within ±0.2 V.
4. Only then connect the load through the DC barrel jack.

## Calibration

The web panel has two calibration fields (`Cal battery` / `Cal 24 V`).
Procedure:

1. Measure the real battery voltage with a multimeter directly at the
   battery terminals.
2. Type the exact reading into the *battery* calibration field and press
   the corresponding button. The firmware computes a per-device
   correction factor and saves it in NVS.
3. Repeat for the 24 V line, measured directly at the AC/DC brick output.

The correction factors are stored as `calVbatt` / `calVgrid` floats
(default 1.000) in NVS and are picked up on the next reading.

## Safety <a id="safety"></a>

- **230 V AC is present inside the enclosure.** Before opening: unplug
  mains, disconnect the battery fuse.
- The AGM battery is a *short-circuit hazard* — hundreds of amps into a
  screwdriver dropped across the terminals. Always disconnect the fuse
  first when working near it.
- Never bypass the fuse. Its job is to isolate a dead-short before the
  battery cables catch fire.
- Do not skip the initial no-load setup of the XL6009 outputs — a wrong
  voltage will kill a router.
- The AGM battery is a consumable. Replace when the measured autonomy
  drops noticeably, the case bulges, or the terminals show corrosion.

## Suggested layout

Keep the AC/DC brick, the XL4016 and the battery physically separated
from the ESP32 and the two XL6009 outputs, or at least route wiring so
that the switching noise from the buck-boost modules doesn't couple into
the ADC dividers. A short common ground point (star ground) near the
battery negative helps a lot.

## Enclosure

A 3D-printable enclosure lives at
[`../hardware/enclosure/`](../hardware/enclosure/). Modelled in
Onshape, distributed as STEP files (import directly in PrusaSlicer /
Bambu Studio / OrcaSlicer), licensed under CC-BY-SA 4.0. Ready-to-paste
listings for Printables and MakerWorld are in the same folder.

Recommended print: **PETG** (or ABS/ASA) — not PLA, because the
charger and both DC/DC modules run warm enough to creep PLA over time.
0.20 mm layers, 4 perimeters, 25–30 % gyroid infill, supports only for
lid cutouts.
