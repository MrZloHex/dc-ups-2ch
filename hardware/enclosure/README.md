# DC-UPS-2CH — enclosure

3D-printable enclosure for the DC-UPS-2CH controller. Modelled in
[Onshape](https://cad.onshape.com/documents/ea66c330322b844216a03713/w/1c92cc427e11be4cf82566dd/e/3ae3fbe0cc714edd3fc95852?renderMode=0&uiState=6a9a1a1390f1add03d0f572a)

<p align="center">
  <img src="renders/exploded-front.png" alt="Exploded isometric — front panel, battery, aluminium extrusion shell" width="49%"/>
  <img src="renders/exploded-rear.png"  alt="Exploded isometric — rear panel with PCB stack" width="49%"/>
</p>

## Contents

```
hardware/enclosure/
├── README.md              this file
├── renders/
│   ├── exploded-front.png     iso view: front panel + battery exploded
│   └── exploded-rear.png      iso view: back panel + PCB stack exploded
├── step/                  neutral STEP exports (for CAD / re-modelling)
│   ├── Main.step              main aluminium-extrusion body
│   ├── Level 1.step           lower PCB tier (charger + XL6009 outputs)
│   ├── Level 2.step           upper PCB tier (ESP32 + I/O)
│   ├── Side 1.step            front panel (IEC C14 inlet + DC output sockets)
│   ├── Side 2.step            rear panel (button + LEDs + cable exits)
│   ├── Battery Holder.step    AGM 12 V / 7 Ah cradle
│   └── Dummy Sky.step         placeholder for the top cover volume
└── 3mf/                   slicer-ready assemblies (drop straight into a slicer)
    ├── DC_UPS.3mf             full assembly view
    ├── Main.3mf, Level 1.3mf, Level 2.3mf, Side 1.3mf, Side 2.3mf,
    └── Battery Holder.3mf, Dummy Sky.3mf
```

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

## License

The 3D model, STEP exports, 3MF slicer packages and renders in this
directory are released under
[Creative Commons Attribution-ShareAlike 4.0 International (CC-BY-SA 4.0)](https://creativecommons.org/licenses/by-sa/4.0/).
This is *separate* from the firmware / documentation licence — see the
top-level [`LICENSE`](../../LICENSE) for those.



