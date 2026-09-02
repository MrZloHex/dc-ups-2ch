# MakerWorld listing — copy/paste template

> Fields below map to the MakerWorld upload flow. Anything between
> `<< >>` needs to be replaced before publishing.

---

## Title

    DC-UPS-2CH — ESP32 backup power controller for router + ONT

## Category

    Gadgets → Electronics Cases

## Hashtags (add to description or the dedicated field)

    #esp32 #ups #backuppower #dcups #router #ont #homelab
    #smarthome #ntfy #esp32project #onshape #electronicsenclosure

## Print profile note (Boost eligibility)

MakerWorld boosts downloads that ship a Bambu Studio project (`.3mf`)
with a validated print profile. This listing currently ships **STEP
only**, which still slices fine in Bambu Studio (File → Open, import
STEP, use the settings below). If you want to unlock the boost:

1. Open a STEP file in Bambu Studio.
2. Apply the print settings from the section below.
3. Save the project as `.3mf`.
4. Attach the `.3mf` alongside the STEP files during upload — MakerWorld
   auto-detects the embedded profile.

## Print settings (paste into the description)

- Printer:        Bambu Lab (any) / any FDM 220 mm build plate or larger
- Filament:       Bambu **PETG HF**, Bambu **ABS**, or Bambu **PAHT-CF**
                  (avoid PLA — the charger inside runs warm)
- Layer height:   0.20 mm
- Wall loops:     4
- Top/bottom:     5 layers each
- Infill:         25–30 % gyroid
- Supports:       tree, only under lid cutouts if needed
- Brim:           5 mm on the case bottom, none on the lid

## License

Model: **CC-BY-SA 4.0** — https://creativecommons.org/licenses/by-sa/4.0/
Firmware (separate): **GPL-3.0-or-later** — https://github.com/<< YOUR_USER >>/dc-ups-2ch/blob/main/LICENSE

## Model source

Editable Onshape document (public, remix-friendly):
<< PASTE PUBLIC ONSHAPE URL HERE >>

---

## Description (Markdown, paste into the big text field)

### What is this?

DC-UPS-2CH is a small **DC uninterruptible power supply** designed for
one very specific job: **keeping a home Wi-Fi router and a fibre ONT
online through short mains outages**. It runs from a 230 V AC input,
keeps a 12 V AGM battery topped up, and drives two independent DC
outputs (typically 12 V for the router and 9–12 V for the ONT).

An ESP32 controller supervises the whole thing and adds features a
passive UPS just can't:

- Automatic mains ↔ battery switching via a shared 13.8 V bus — no
  clunky relay.
- **Two independent outputs**, each with manual `ON` / `OFF` / `AUTO`
  and a "restart just this channel for 5 seconds" button.
- Configurable **low-voltage disconnect** with hysteresis and a
  fallback deep-sleep so a flat battery doesn't get any flatter.
- **Auto-recovery**: no Wi-Fi → power-cycle the router; Wi-Fi OK but
  WAN dead → power-cycle the ONT; then both; then optionally reboot
  the ESP itself. Counters reset after 10 minutes of stable connectivity.
- **ntfy.sh push notifications** for outages, LVD, boot, sleep and
  recovery events. Read-only `!ups status` / `!ups ping` commands come
  back over the same topic.
- Local web panel at `http://dc-ups.local/` (mDNS), with a captive
  setup AP called `DC-UPS-Setup` for first-time configuration.
- Physical **Shelf Sleep** mode: hold the button 10 s and everything
  turns off; only another press wakes it up. Perfect for storage or
  transport without draining the battery.

### What's in this listing

The **enclosure only** — STEP files for the case body, lid and any
mounting brackets. Bambu Studio imports STEP directly, so you can
slice without hunting for STL. The full CAD lives on Onshape (link
above) so anyone can fork it in a real CAD editor and remix without
reverse-engineering meshes.

### Everything else

Firmware, wiring diagrams, BOM, calibration procedure and the full
Russian device passport with service checklists live in the GitHub
repo:

<< PASTE PUBLIC GITHUB REPO URL HERE >>

TL;DR bill of materials for the electronics side:

- ESP32 DevKit v1 (USB-C or micro)
- AC/DC 230 V → 24 V brick, ≥ 60 W
- XL4016 CC/CV step-down (13.8 V @ ~1.5 A)
- AGM/SLA 12 V 7 Ah battery
- 2 × XL6009 buck-boost (one per output)
- 2 × IRF9Z34N (or equivalent) P-MOSFETs + 2 × 2N2222 NPN drivers
- Voltage divider resistors, LEDs, tact switch, a 5 A automotive fuse

### Print notes for Bambu printers

- **PETG HF** on a smooth PEI plate is the sweet spot — heat-resistant
  enough for the case, easy to print, no chamber needed.
- If you have an X1C / P1S with a full chamber, **ABS** or **PAHT-CF**
  give you a properly rigid case.
- **Do not print in PLA.** The XL4016 charger and the XL6009 modules
  get warm enough that PLA will creep over months.
- Orient the case bottom **opening up** — the battery bay and internal
  ribs then print support-free. Tree supports only under the lid cutouts.

### Assembly

Step-by-step is in `docs/HARDWARE.md` on GitHub. Short version:

1. Print the case; add threaded inserts if your CAD variant uses them.
2. Mount the AC/DC brick, XL4016 charger, ESP32 and both XL6009s.
3. Wire the P-MOSFET high-side switches per the diagram in `HARDWARE.md`.
4. Add the 5 A fuse **at the battery `+` terminal, as close as possible**.
5. Battery goes in **last**, after every output voltage is verified with
   a multimeter under no load.

### Safety

Live 230 V AC is inside the case. **Unplug mains and disconnect the
battery fuse** before opening. An SLA battery can dump hundreds of
amps into a short — the fuse is non-negotiable.

### Licenses

- The model (this listing) is **CC-BY-SA 4.0**. Remix freely, credit
  back, and share alike.
- The firmware in the GitHub repo is **GPL-3.0-or-later** — if you
  distribute a device with modified firmware, you must publish your
  source under the same license.

### Remixes welcome

Especially: DIN-rail brackets, alternative battery bays (12 V 9 Ah,
LiFePO4 pack), wall-mount ears, and any variant with a display cutout.
Open an issue on GitHub if you'd like it linked from the main repo.
