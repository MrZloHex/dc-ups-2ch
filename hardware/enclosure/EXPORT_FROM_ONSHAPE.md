# Exporting the enclosure from Onshape

The Onshape document is the source of truth. STEP files under `step/`
are re-exported from it — do not edit the STEP files by hand.

## One-time setup

1. Make sure the Onshape document is **public** (Share → General access
   → *Public*). The listing links on Printables / MakerWorld send users
   back to this document.
2. In each Part Studio you plan to export, make sure the correct part
   has its **material** and **units set to millimetres**.
3. If you rename a part, update `hardware/enclosure/README.md` and both
   listing templates so users know what to expect in the ZIP.

## Per-part export

For every part or assembly you want to publish:

1. Open the Part Studio (for individual parts) or Assembly.
2. Right-click the part / assembly tab at the bottom → **Export**.
3. Format: **STEP**. Units: **Millimeter**. Zip: **No**.
4. Save into `hardware/enclosure/step/` with a name that matches the
   pattern already used, e.g. `dc-ups-2ch-<part>.step`.

## Bundle for the listing

Printables and MakerWorld both take a single ZIP or the raw files.
Suggested bundle name: `dc-ups-2ch-enclosure-YYYY-MM-DD.zip`, containing
everything under `step/` plus a copy of `LICENSE-CC-BY-SA-4.0.txt` and
`README.md` from this folder.

```bash
cd hardware/enclosure
zip -r dc-ups-2ch-enclosure-$(date +%F).zip \
    step/*.step \
    README.md \
    LICENSE-CC-BY-SA-4.0.txt
```

Upload that ZIP as the "printable files" attachment on Printables and
MakerWorld. On MakerWorld you can additionally attach a `.3mf` Bambu
Studio project file with a print profile — that unlocks the download
boost. See [MAKERWORLD.md](MAKERWORLD.md) for details.

## Regenerating after design changes

1. Push CAD changes in Onshape.
2. Re-run the export steps above for every changed part.
3. `git add hardware/enclosure/step/ && git commit -m "enclosure: update step exports"`.
4. If you had a live Printables/MakerWorld listing, upload the new ZIP
   as a new version (both platforms preserve history and let users see
   the diff).
