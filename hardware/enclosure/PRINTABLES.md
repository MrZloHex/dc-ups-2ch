# Printables listing — copy/paste template

> Fields below map 1:1 to the Printables upload form. Anything between
> `<< >>` needs to be replaced before publishing.

---

## Title

    DC-UPS-2CH — ESP32 DC UPS enclosure for router + ONT

## Summary (short description, ~200 chars)

    3D-printable enclosure for the DC-UPS-2CH — a two-channel DC UPS
    that keeps your Wi-Fi router and ONT online during mains outages,
    with an ESP32 web panel, ntfy push notifications and auto-recovery.

## Category

    Gadgets → Electronics

## Tags

    esp32, ups, backup-power, dc-ups, router, ont, homelab,
    smart-home, ntfy, esp32-project, onshape, remixable

## Print settings

- Printer:            any FDM 220 × 220 × 250 mm or larger
- Rafts:              no
- Supports:           only for lid cutouts (case bottom prints support-free)
- Resolution:         0.20 mm
- Infill:             25–30 % gyroid
- Filament:           **PETG / ABS / ASA** — not PLA (charger runs warm)
- Walls:              4 perimeters
- Top/bottom layers:  5 each
- Brim:               5 mm on the case bottom, none on the lid

## License

Model: **CC-BY-SA 4.0** — https://creativecommons.org/licenses/by-sa/4.0/
Firmware (separate): **GPL-3.0-or-later** — https://github.com/<< YOUR_USER >>/dc-ups-2ch/blob/main/LICENSE

## Model source

Editable Onshape document (public, remix-friendly):
<< PASTE PUBLIC ONSHAPE URL HERE >>

---

## Description (Markdown, paste into the big text field)

DC-UPS-2CH is a small **DC uninterruptible power supply** built for the
one job most consumer UPSs do badly: **keep a home Wi-Fi router and a
fibre ONT online through short mains outages**. It runs from a 230 V AC
input, keeps a 12 V AGM battery topped up, and drives two independent
DC outputs (typically 12 V for the router and 9–12 V for the ONT).

An ESP32 controller supervises everything and gives you visibility you
just don't get from a passive UPS board:

- Automatic mains ↔ battery switching (no clunky relay — it's a shared
  13.8 V bus).
- **Two independent outputs**, each with manual `ON` / `OFF` / `AUTO`
  and a "restart this channel for 5 seconds" button.
- **Low-voltage disconnect** with configurable thresholds, hysteresis
  and a fallback deep-sleep so you don't murder the battery.
- **Auto-recovery**: if the router loses Wi-Fi it gets a power cycle,
  if the WAN dies the ONT gets one, then both, then optionally the ESP.
- **ntfy.sh push notifications** for outages, LVD, recovery actions —
  and read-only `!ups status` / `!ups ping` commands back to the device
  over the same topic.
- Local web panel at `http://dc-ups.local/` (mDNS) with a captive-portal
  setup AP when Wi-Fi isn't configured.
- A physical **Shelf Sleep** mode: hold the button 10 s and everything
  turns off; only another press wakes it back up. Perfect for storage
  or transport without draining the battery.

### What's in this listing

The **enclosure only** — STEP files for the case body, lid and any
brackets. Slicers like PrusaSlicer, Bambu Studio and OrcaSlicer import
STEP directly, so you don't need a separate STL. The full CAD lives
in Onshape (link above) so anyone can remix without reverse-engineering
meshes.

### Everything else lives on GitHub

Firmware, wiring diagram, BOM, calibration procedure and the full
Russian passport with service checklists:

<< PASTE PUBLIC GITHUB REPO URL HERE >>

TL;DR bill of materials for the electronics:

- ESP32 DevKit v1 (USB-C or micro — either)
- AC/DC 230 V → 24 V brick, ≥ 60 W
- XL4016 CC/CV step-down, set to 13.8 V @ ~1.5 A
- AGM/SLA 12 V 7 Ah battery
- 2 × XL6009 buck-boost (one per output)
- 2 × IRF9Z34N (or equivalent) P-MOSFETs + 2 × 2N2222 NPN drivers
- Voltage divider resistors, LEDs, tact switch — the usual bits

### Print notes

- **Do not print in PLA.** The XL4016 charger and the XL6009 modules
  get warm enough that PLA will creep over time. PETG is the sweet
  spot; ABS/ASA if you have an enclosed printer.
- Orient the case bottom **opening up** so the battery bay and internal
  ribs print without any supports.
- The lid needs supports only under the LED / button cutouts, if you
  chose that variant.
- 4 perimeters + 25 % gyroid infill is plenty stiff for wall-mount.

### Assembly

Full step-by-step is in `docs/HARDWARE.md` on GitHub. Short version:

1. Print the case, insert threaded inserts if your CAD variant uses them.
2. Mount the AC/DC brick, XL4016 charger, ESP32 board and both XL6009s.
3. Wire the P-MOSFET high-side switches per the ASCII diagram in
   `HARDWARE.md`.
4. Add the 5 A fuse **at the battery `+` terminal, as close as possible**.
5. Battery goes in last, after you've verified every output voltage with
   a multimeter under no load.

### Safety

Live 230 V AC is present inside the case. **Unplug mains and disconnect
the battery fuse** before opening. An SLA battery can dump hundreds of
amps into a short — that's why the fuse is non-negotiable.

### License

- The model (this listing) is **CC-BY-SA 4.0**. Remix freely, credit
  back, and re-share under the same terms.
- The firmware in the linked GitHub repo is **GPL-3.0-or-later** — if
  you distribute a device that runs modified firmware, you need to
  publish your source under the same license.

### Feedback

Prints, remixes and issues welcome. The GitHub repo has the issue
tracker; hardware quirks and wall-mount variants make especially useful
remixes.
