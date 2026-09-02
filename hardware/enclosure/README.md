# DC-UPS-2CH — enclosure

3D-printable enclosure for the DC-UPS-2CH controller. Modelled in
[Onshape](https://onshape.com/) and exported as STEP so anyone can remix
in real CAD; slicers (PrusaSlicer, Bambu Studio, OrcaSlicer, Cura ≥ 5.7)
all import STEP directly.

- **License:** [CC-BY-SA 4.0](LICENSE-CC-BY-SA-4.0.txt)
- **Source of truth:** the public Onshape document (link in
  [PRINTABLES.md](PRINTABLES.md) / [MAKERWORLD.md](MAKERWORLD.md))
- **Distributed files:** `step/*.step` (see below)

## Contents

```
hardware/enclosure/
├── LICENSE-CC-BY-SA-4.0.txt         model license
├── README.md                        this file
├── PRINTABLES.md                    ready-to-paste Printables listing
├── MAKERWORLD.md                    ready-to-paste MakerWorld listing
├── EXPORT_FROM_ONSHAPE.md           how to regenerate the STEP exports
└── step/
    ├── dc-ups-2ch-assembly.step     full assembly for visualisation
    ├── dc-ups-2ch-case-bottom.step  main body with battery bay
    ├── dc-ups-2ch-case-top.step     lid with vents and button/LED cutouts
    └── ...                          any additional brackets / clips
```

If you rename a part in Onshape, re-export it and update this list.

## Print settings (baseline)

| Setting          | Value                                                    |
|------------------|----------------------------------------------------------|
| Material         | PETG (preferred) or ABS/ASA — this thing sits next to a  |
|                  | charger that gets warm. **Not PLA** for the case body.   |
| Nozzle           | 0.4 mm                                                   |
| Layer height     | 0.20 mm                                                  |
| Walls            | 4 perimeters (strength around threaded inserts)          |
| Top/bottom       | 5 layers each                                            |
| Infill           | 25–30 % gyroid                                           |
| Supports         | Only for lid cutouts if needed — orient case bottom      |
|                  | opening-up so the battery bay prints support-free        |
| Brim             | 5 mm on the case bottom, none on the lid                 |

Tune to your printer. These are conservative and prioritise heat resistance
over speed.

## Adding STL / 3MF

Only STEP is shipped by design (it's the most useful for remixing).
If a downstream user needs STL/3MF, either:

1. Drop a STEP into any slicer and export STL from there, or
2. Re-export from Onshape as STL/3MF and drop the files into
   `step/` (or a sibling `stl/` folder — update this README either way).
