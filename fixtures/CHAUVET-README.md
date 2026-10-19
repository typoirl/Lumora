# Chauvet DJ & Chauvet Professional fixture profiles for Lumora

Generated 15 Oct 2026 from the official user manual PDFs linked on each product page of chauvetdj.com and chauvetprofessional.com.

## What's in here
- `chauvet-dj/`: 134 fixtures
- `chauvet-professional/`: 126 fixtures
- 1409 DMX modes in total

There is one file per fixture, holding every channel mode charted in its manual. Every file was loaded and checked with Lumora's own fixture loader.

The files use the Open Fixture Library JSON format, which Lumora imports directly. Pixel and multi-cell fixtures carry a `matrix`, so Lumora splits them into sub-fixtures. Fine channels are linked to their coarse channels as 16-bit pairs.

## Not possible
These products have no user manual posted, or the manual has no usable DMX chart:

DJ Bank, Duo Moon, EZBar, EZLink FSBT, EZLink Par Q1BT Pack, EZPin Pack 4, EZPin Zoom Pack, Freedom Flex Battery H9, Funfetti Shot, Hurricane 1000, Hurricane 1200, Hurricane Bubble Haze LT, LED Pinspot 2, Mini Strobe LED, SlimPACK 56 LT, SlimPACK Q12 ILS, SlimPAR Q6 ILS, EPIX Strip Tour, EPIX Strip IP, onAir Producer, Ovation 2WL, Ovation Reve E2, Ovation Reve F2, Ovation Reve H1, Ovation Reve H2, Ovation Reve P2 IP, Nimbus (manual has no DMX chart), Swarm Wash FX ILS (the manual PDF could not be read; its quick-reference guide has no DMX chart).

## Assumptions and gaps
- shocker-panel-fx: chart copied from the Shocker Panel FX IP manual (same modes on the product page); the non-IP manual wasn't transcribed separately.
- festoon-2-rgb: the Pixel and Pixel+ modes are written as 61 RGB pixels (183 = 61 x 3); the manual's text said 60 cells, which doesn't fit 183 channels.
- intimidator-trio-xd: the manual lists only 13 of the 16CH mode's channels; channels 14-16 are left empty.
- led-shadow2-ils, mini-kinta-ils: the product pages list an 8-channel mode the manuals don't chart; only the charted 4-channel mode is included.
- COLORado PXL Bar 16: Color/Strobe/Control/LED-macro sub-charts were not readable in its manual; the identical charts from the PXL Bar 8 manual were used.
- COLORado PXL Curve 12: Strobe/Control/LED-macro sub-charts truncated in the manual; PXL Bar 8 charts used (same family).
- COLORado PXL Bar/Curve strobe "Off" (000-019) treated as shutter closed, "On" as open.
- COLORado PXL Curve 5: each of the 5 heads is a cell ("Head n"), rings are "Head n Ring m" cells; ch 6 of heads 2-5 is Reserved.
- COLORado 3 Quad Zoom "Hue" in 5/11/13/16-ch modes is a +/-25 green-magenta shift, named Tint.
- COLORado Solo Bar 4 / Solo Bar 6: manuals' channel maps were truncated in text extraction; the per-cell layouts were built from the complete Solo Bar 1 chart, and every mode's channel count was checked against the mode list in the Bar 4/6 manuals.
- COLORado Solo Edge 3: built from the Solo Edge 1 chart pattern and the Edge 3 mode list (all channel counts match).
- COLORado Solo Pod E: strobe values are inline in the manual and weren't readable; used 000-010 no function, 011-255 strobe slow-fast (as on the COLORado 3 Quad Zoom).
- EPIX Flex Drive "Pixel" mode: one DMX universe addresses 170 RGB pixels (510 ch); further pixels need more universes (Art-Net/sACN).
- EPIX Line 10 IP: the manual's personality table (150/30/15/6/3 ch) disagrees with its channel tables (300/60/30/12/6 ch); the channel tables were used.
- EPIX Line 10 IP Effects Engine: the three effect colors are modelled as cells "Color 1-3" (RGBW each).
- Maverick Storm 2 BeamWash: the manual's strobe and control sub-charts weren't readable; strobe uses the Maverick Force 2 / COLORado PXL chart, control is a single generic range, and the color chart's 201-255 tail was completed from the same family chart. Ring pixels are cells "Ring 1-12", main pixels "Main 1-19".
- Maverick Storm 1 Flex: 26-ch ranges are the same as the 33-ch ranges.
- Moving heads whose strobe chart says "Off/On" have 000-xxx treated as shutter closed and "On" as open.
- Maverick Storm 3 BeamWash: the manual's dual-personality tables and strobe/color/control sub-charts were missing from the text; Dual modes were rebuilt from the Storm 2 BeamWash pattern (all channel counts match the Storm 3 manual's list), Full PXL has Control on ch 8 (gives the stated 424 ch), and strobe/color charts come from the Storm 2 / Force 2 family.
- Ovation E-2 FC: the HSV mode's channel table prints Dimmer/Saturation/Value while the menu map says Hue/Saturation/Value; Hue/Saturation/Value was used.
- Ovation E-910FC: the extracted 5Ch/7Ch tables were inconsistent with the menu map; the standard Ovation layout (5Ch = RGBAL, 7Ch = Dimmer + RGBAL + Strobe), as on the E-910FC IP and CYC 1 FC, was used.
- Ovation F-415VW / F-915VW: the Orange emitter has no matching color attribute in Lumora, so it loads as a control channel (Ctl_Orange); Royal blue loads as Indigo.
- Rogue R2X Wash: its manual uses the same zone/color/program layout as the Rogue Outcast 2X Wash; the Outcast 2X Wash zone-selection and program charts were used.
- Rogue R2X Wash VW: the 23CH table in the manual lists only 22 channels; Control was added as ch 23 to match the mode name and the 23CH MS layout. Zone-selection values follow the Rogue 2X Wash family chart; program slots 25-48 are labelled generically.
- Rogue R2E Spot: the static gobo wheel's shake ranges and two movement-macro ranges were garbled in the manual text; they were rebuilt with the same step sizes as the R1E Spot.
- Rogue Outcast 1M Beam: endless pan/tilt rotation channels load as a second Pan/Tilt attribute, since Lumora has no separate continuous-rotation attribute.
- STRIKE Array family: per-pod strobe channels aren't charted in the manuals; they use 000-010 no function / 011-255 strobe slow-fast.
- STRIKE Array 2C / 4C: the 5 Ch mode is charted as Dimmer + RGBA (no white), although the menu text says RGBAW; the chart was followed.
- STRIKE Array Ultra: the extracted strobe chart read "On" at 000-019 and "Off" at 020-024; the family convention (closed at 000-019, open at 020-024) was used.
- STRIKE Bolt 1C: the 16 beam LEDs are cells "Beam 1-16" (25Ch pairs them as "Beam n+n+8"), the two plates are cells "Plate 1-2".
- STRIKE V: the manual text stops partway through the 222CH/314CH pixel tables and doesn't chart the Movement, Dual Pixels or Multi-Layer personalities. The pixel layout (24 Dots then 24 Cells) follows the manual's channel counts; Movement 30CH reuses the 222CH header; Dual Pixels 144/192/240/288 are 48 pixels of RGB / RGBW / Dim+RGBW / Dim+fine+RGBW; Multi-Layer modes are given as separate Dot and Cell layer modes (24 pixels each). Color-temperature, color-macro and FX-macro charts weren't readable, so those channels have a single range. Verify these modes on a real unit.
- Intimidator Trio XD: the 16CH mode's channels 14-16 are left empty because the manual doesn't chart them.

## Things to know in Lumora
- LED fixtures with discrete Cyan, Magenta or Yellow emitters load as CMY (subtractive) attributes. That is how Lumora's OFL loader maps those colors.
- These have no dedicated attribute in Lumora, so they appear as control channels (`Ctl_...`):
  - framing blades and frame rotation
  - tint, hue and saturation
  - X/Y color coordinates
  - virtual strobe
  - orange emitters
- A second prism loads as `Prism12` because Lumora's loader always maps the prism type to Prism1.

## Gobo pictures
Gobo images come from the manufacturers' GDTF files on GDTF Share (downloaded 17 Oct 2026), matched to the wheel slots by gobo number and scaled to 128 px PNGs. They're stored in each file's top-level `images` object and named by the slot's `image` key; shake ranges use the same picture.

Fixtures with pictures: Intimidator Beam 360X, Hybrid 251SR, Spot 260X, Spot 360X, Spot 375ZX, Spot 475ZX, Spot 60 ILS; Maverick Force X Profile, Force X Spot, Silens 1X Profile, Silens 2X Profile, Storm 1 Beam, Storm 1 Flex, Storm 1 Hybrid, Storm 2 Profile, Storm 3 Profile, Storm 4 Profile, Storm 4 SoloWash; Rogue Outcast 1L Beam, Outcast 1M Beam, Outcast 2 Beam, Outcast 2 Hybrid, Outcast 3 Spot, R3 Beam, R3E Spot.

No pictures available: Rogue R1E Spot and R2E Spot (their GDTF files' gobo images are empty), Rogue Outcast 3X Wash, Maverick Force 2 BeamWash and the Ovation gobo slots (no images in their GDTF files), and Blazor, Intimidator Free Spot 60 ILS, Scan 110, Scan 360, Spot 110, Spot 160 ILS, Spot 360QD, Spot 360X IP, Maverick Storm 2 Profile M, Obsession HP and Sentinel Spot Q60 ILS (no GDTF file on GDTF Share).
