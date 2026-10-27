# TurtleGuard upper enclosure

This folder contains a parametric first-pass upper enclosure for the current
TurtleGuard electronics. The source is `turtleguard_upper.scad` and uses
millimetres throughout.

## What the design provides

- A turtle-shaped 240 x 160 x 68 mm carapace with a 3 mm nominal wall.
- A flat flange, four recessed M3 lid holes, and a downward sealing tongue.
- A low cradle for a 142 x 68 x 18 mm power bank, with two hook-and-loop strap
  paths. The battery mass stays close to the tripod axis.
- A removable 104 x 72 mm electronics bridge above the power bank, with four
  M2.5/M3-style bosses and four zip-tie points. Its default hole pattern is for
  a 90 x 60 mm perfboard.
- A raised, separately printed 46 x 36 mm GPS sled for a common NEO-6M board
  and ceramic patch antenna. It sits toward the head with the patch facing up.
- Three roof optical ports: TSL2584 sea, TCS3448 colour, and TSL2584 land.
  Each has a recessed 17 mm clear window and a two-screw internal cartridge.
- Rain-sheltered head vents for the SHT45 environmental sensor.
- Three 12.5 mm rear cable-gland holes for the DS18B20 bus, soil probes, and
  power/service wiring.
- A separate 52 mm square lower-base tripod reinforcement with a captive metal
  1/4-20 nut pocket, anti-rotation key, and four M4 load-transfer holes.
- Four flippers, a head, and a tail that make the installed unit read clearly
  as a turtle without consuming the electronics cavity.

## Before printing

This is deliberately a **dimensioned prototype**, because the repository does
not yet contain CAD for the lower enclosure, the perfboard, or the exact sensor
breakouts. Measure these items with calipers and update the named parameters at
the top of the SCAD file:

1. Mating-base length, width, flange location, screw centres, and lip/groove.
2. Actual circuit-board outline and mounting-hole centres.
3. Power-bank length, width, thickness, USB-port location, and minimum cable
   bend radius.
4. NEO-6M board outline and its mounting-hole centres (if present).
5. Optical breakout sizes, screw centres, active-sensor location, and required
   field of view.
6. Cable-gland thread diameter and the installed cable bend radius.
7. Lower-base wall thickness and the actual tripod adapter/release-plate shape.

Allow at least 0.25-0.40 mm clearance per side for FDM mating parts. Increase
that for printers with elephant-foot or dimensional-calibration issues.

## Export

Open the SCAD file in OpenSCAD, change `part` near the top, render with F6, and
export each part as STL:

1. `lid`
2. `electronics_sled`
3. `gps_sled`
4. `optical_cartridge` (print three)
5. `env_cartridge`
6. `tripod_adapter`

Use the `assembly` setting only to inspect fit; do not export it as one print.

On Windows, `export_stls.ps1` renders all six printable parts when OpenSCAD is
installed:

```powershell
.\export_stls.ps1
```

If OpenSCAD is portable or installed elsewhere, pass its executable path with
`-OpenScadPath`. The generated files go into the local `stl` subfolder.

The default `production` export preserves the original smooth 72-segment
geometry. For quicker fit checks on a slower computer, use:

```powershell
.\export_stls.ps1 -Quality draft
```

Draft mode uses fewer curve segments but does not change any nominal dimension.

## Suggested print and assembly

- Material: ASA for outdoor UV/heat; PETG is acceptable for prototypes. Avoid
  PLA for a sun-exposed field unit.
- Layer height: 0.20 mm; four perimeters; five top/bottom layers; 25% gyroid.
- Print the lid flange-down. Enable supports only for the small roof sensor
  bosses; the main carapace uses gradual curves.
- Print the separate sled/cartridges flat.
- Use two hook-and-loop straps around the power bank. Do not depend on a tight
  printed friction fit, tape, or the USB cable to retain it.
- Bond 0.5-1.0 mm UV-stable clear polycarbonate discs into the optical recesses
  with a flexible outdoor sealant. Do not put sealant over the optical path.
- Use closed-cell 2 mm silicone/EPDM gasket cord between upper and lower
  flanges. The printed tongue is not waterproof by itself.
- Put a breathable hydrophobic membrane behind the head vents. An exposed
  SHT45 will otherwise be damaged by driven rain or condensation.
- Keep the GPS ceramic patch facing the sky. Keep the regulator, ESP8266
  antenna, and long wiring away from and below it.
- Add strain relief inside every cable gland and make a drip loop outside.
- Put the tripod insert in the **lower enclosure**, directly under the power
  bank/combined centre of mass. Do not hang the unit from the removable lid or
  from the four M3 sealing screws.
- Use a captive metal 1/4-20 UNC nut or purpose-made camera insert. Printed
  threads are not adequate for an outdoor tripod joint. Use the four M4 holes
  and a backing plate to spread bending load into the lower enclosure.
- Confirm that the chosen tripod and head are rated above the complete unit's
  mass and overturning moment, including wind on the shell and attached cables.
- Do not charge or operate a damaged/swollen power bank. Validate its allowed
  charge/discharge temperature, provide thermal monitoring, and avoid leaving
  a charging lithium pack in a sealed enclosure in direct sun.

The shell is not yet certified watertight or load-bearing. Perform a dry fit,
spray test, thermal soak, GPS cold-start test, and sensor cross-check before
field deployment.

