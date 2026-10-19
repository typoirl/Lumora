#!/usr/bin/env python3
"""Builds fixtures/gdtf/Generic@GDTF_Sample_Spot.gdtf: a small but valid GDTF 1.2 moving spot
with drawn gobo images, used by the demo show and the GDTF import test.
Run: python3 tools/make_sample_gdtf.py  (needs Pillow)"""
import io, math, os, zipfile
from PIL import Image, ImageDraw

def gobo(kind, size=128):
    # Gobo images: white = light passes, black = metal. Drawn at 4x and downsampled for smooth edges.
    s = size * 4
    img = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(img)
    c = s / 2
    r = s * 0.46
    if kind == "dots":
        for ring, count, dr in ((0, 1, 0.09), (0.28, 6, 0.07), (0.55, 12, 0.06), (0.8, 18, 0.045)):
            for i in range(count):
                a = 2 * math.pi * i / count
                x, y = c + math.cos(a) * r * ring, c + math.sin(a) * r * ring
                d.ellipse([x - s * dr, y - s * dr, x + s * dr, y + s * dr], fill=255)
    elif kind == "breakup":
        d.ellipse([c - r, c - r, c + r, c + r], fill=255)
        import random
        rnd = random.Random(7)
        for _ in range(26):
            x, y = rnd.uniform(c - r, c + r), rnd.uniform(c - r, c + r)
            w = rnd.uniform(s * 0.04, s * 0.13)
            pts = [(x + math.cos(a) * w * rnd.uniform(0.6, 1.3), y + math.sin(a) * w * rnd.uniform(0.6, 1.3))
                   for a in [k * 2 * math.pi / 7 for k in range(7)]]
            d.polygon(pts, fill=0)
    elif kind == "ring":
        d.ellipse([c - r * 0.92, c - r * 0.92, c + r * 0.92, c + r * 0.92], fill=255)
        d.ellipse([c - r * 0.62, c - r * 0.62, c + r * 0.62, c + r * 0.62], fill=0)
    elif kind == "star":
        pts = []
        for i in range(10):
            a = -math.pi / 2 + i * math.pi / 5
            rr = r * (0.95 if i % 2 == 0 else 0.4)
            pts.append((c + math.cos(a) * rr, c + math.sin(a) * rr))
        d.polygon(pts, fill=255)
    elif kind == "triangles":
        for i in range(3):
            a = -math.pi / 2 + i * 2 * math.pi / 3
            x, y = c + math.cos(a) * r * 0.5, c + math.sin(a) * r * 0.5
            tri = [(x + math.cos(a + k * 2 * math.pi / 3) * r * 0.38, y + math.sin(a + k * 2 * math.pi / 3) * r * 0.38) for k in range(3)]
            d.polygon(tri, fill=255)
    # Circular aperture
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).ellipse([c - r, c - r, c + r, c + r], fill=255)
    img = Image.composite(img, Image.new("L", (s, s), 0), mask).resize((size, size), Image.LANCZOS)
    out = io.BytesIO()
    img.convert("RGB").save(out, "PNG")
    return out.getvalue()

GOBOS = [("Dots", "gobo_dots", "dots"), ("Breakup", "gobo_breakup", "breakup"), ("Ring", "gobo_ring", "ring"),
         ("Star", "gobo_star", "star"), ("Triangles", "gobo_triangles", "triangles")]
COLORS = [("Red", "0.6400,0.3300,21.3"), ("Blue", "0.1500,0.0600,7.2"), ("Green", "0.3000,0.6000,71.5"),
          ("Amber", "0.5600,0.4300,45.0"), ("Magenta", "0.3200,0.1500,28.5")]

def channel(name, offsets, attr, functions, highlight="None", geometry="Head"):
    off = ",".join(str(o) for o in offsets)
    initial = f"{geometry}_{attr}.{attr}.{functions[0][0]}"
    xml = [f'        <DMXChannel DMXBreak="1" Offset="{off}" InitialFunction="{initial}" Highlight="{highlight}" Geometry="{geometry}">',
           f'          <LogicalChannel Attribute="{attr}" Snap="No" Master="None" MibFade="0.000000" DMXChangeTimeLimit="0.000000">']
    for fname, fattr, dmxfrom, default, wheel, sets in functions:
        w = f' Wheel="{wheel}"' if wheel else ""
        xml.append(f'            <ChannelFunction Name="{fname}" Attribute="{fattr}" OriginalAttribute="" DMXFrom="{dmxfrom}" '
                   f'Default="{default}" PhysicalFrom="0.000000" PhysicalTo="1.000000" RealFade="0.000000"{w}>')
        for sname, sfrom, slot in sets:
            si = f' WheelSlotIndex="{slot}"' if slot else ""
            xml.append(f'              <ChannelSet Name="{sname}" DMXFrom="{sfrom}"{si}/>')
        xml.append('            </ChannelFunction>')
    xml += ['          </LogicalChannel>', '        </DMXChannel>']
    return "\n".join(xml)

def mode(name, fine):
    o = iter(range(1, 64))
    def take(n): return [next(o) for _ in range(n)]
    res = 2 if fine else 1
    full = "65535/2" if fine else "255/1"
    half = "32768/2" if fine else "128/1"
    zero = "0/2" if fine else "0/1"
    chans = [
        channel("Pan", take(res), "Pan", [("Pan", "Pan", zero, half, None, [])], geometry="Yoke"),
        channel("Tilt", take(res), "Tilt", [("Tilt", "Tilt", zero, half, None, [])]),
        channel("Dimmer", take(res), "Dimmer", [("Dimmer", "Dimmer", zero, zero, None, [])], highlight=full),
        channel("Shutter", take(1), "Shutter1", [("Closed", "Shutter1", "0/1", "32/1", None, [("Closed", "0/1", 0), ("Open", "32/1", 0)]),
                                                ("Strobe", "Shutter1Strobe", "64/1", "64/1", None, [])], highlight="32/1"),
        channel("Color", take(1), "Color1", [("Color wheel", "Color1", "0/1", "0/1", "Color1",
                                              [("Open", "0/1", 1)] + [(n, f"{16 * (i + 1)}/1", i + 2) for i, (n, _) in enumerate(COLORS)]),
                                             ("Color spin", "Color1WheelSpin", "128/1", "128/1", None, [])]),
        channel("Gobo", take(1), "Gobo1", [("Gobo wheel", "Gobo1", "0/1", "0/1", "Gobo1",
                                            [("Open", "0/1", 1)] + [("", f"{20 * (i + 1)}/1", i + 2) for i in range(len(GOBOS))]),
                                           ("Gobo shake", "Gobo1SelectShake", "128/1", "128/1", "Gobo1", [])]),
        channel("Gobo rotation", take(1), "Gobo1Pos", [("Gobo index", "Gobo1Pos", "0/1", "128/1", None, [])]),
        channel("Prism", take(1), "Prism1", [("Prism out", "Prism1", "0/1", "0/1", None, [("Out", "0/1", 0)]),
                                             ("3-facet", "Prism1", "128/1", "128/1", None, [("3-facet", "128/1", 0)])]),
        channel("Zoom", take(1), "Zoom", [("Zoom", "Zoom", "0/1", "128/1", None, [])]),
        channel("Focus", take(1), "Focus1", [("Focus", "Focus1", "0/1", "128/1", None, [])]),
        channel("Control", take(1), "Control1", [("No function", "Control1", "0/1", "0/1", None, []),
                                                 ("Reset", "Control1", "200/1", "200/1", None, [("Reset", "200/1", 0)])]),
    ]
    return f'    <DMXMode Name="{name}" Geometry="Base">\n      <DMXChannels>\n' + "\n".join(chans) + '\n      </DMXChannels>\n    </DMXMode>'

wheels = ['    <Wheel Name="Color1">', '      <Slot Name="Open" Color="0.312700,0.329000,100.000000"/>']
wheels += [f'      <Slot Name="{n}" Color="{c}"/>' for n, c in COLORS]
wheels += ['    </Wheel>', '    <Wheel Name="Gobo1">', '      <Slot Name="Open" Color="0.312700,0.329000,100.000000"/>']
wheels += [f'      <Slot Name="{n}" Color="0.312700,0.329000,100.000000" MediaFileName="{m}"/>' for n, m, _ in GOBOS]
wheels += ['    </Wheel>']

attrs = ["Pan", "Tilt", "Dimmer", "Shutter1", "Shutter1Strobe", "Color1", "Color1WheelSpin", "Gobo1",
         "Gobo1SelectShake", "Gobo1Pos", "Prism1", "Zoom", "Focus1", "Control1"]
desc = f'''<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<GDTF DataVersion="1.2">
  <FixtureType Name="GDTF Sample Spot" ShortName="Sample Spot" LongName="GDTF Sample Spot" Manufacturer="Generic"
    Description="Sample moving spot shipped with Lumora to demonstrate GDTF import" FixtureTypeID="6F4C0D2E-7B21-4C8E-9C1A-5D3B2F9A1E77"
    Thumbnail="" RefFT="" CanHaveChildren="Yes">
  <AttributeDefinitions>
    <Attributes>
{chr(10).join(f'      <Attribute Name="{a}" Pretty="{a}" Feature="Control.Control"/>' for a in attrs)}
    </Attributes>
  </AttributeDefinitions>
  <Wheels>
{chr(10).join(wheels)}
  </Wheels>
  <PhysicalDescriptions/>
  <Models/>
  <Geometries>
    <Geometry Name="Base" Model="" Position="{{1,0,0,0}}{{0,1,0,0}}{{0,0,1,0}}{{0,0,0,1}}">
      <Axis Name="Yoke" Model="" Position="{{1,0,0,0}}{{0,1,0,0}}{{0,0,1,0}}{{0,0,0,1}}">
        <Axis Name="Head" Model="" Position="{{1,0,0,0}}{{0,1,0,0}}{{0,0,1,0}}{{0,0,0,1}}"/>
      </Axis>
    </Geometry>
  </Geometries>
  <DMXModes>
{mode("Extended", True)}
{mode("Basic", False)}
  </DMXModes>
  <Revisions/>
  <FTPresets/>
  <Protocols/>
  </FixtureType>
</GDTF>
'''
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "..", "fixtures", "gdtf", "Generic@GDTF_Sample_Spot@v1.gdtf")
with zipfile.ZipFile(out, "w") as z:
    z.writestr(zipfile.ZipInfo("description.xml"), desc, compress_type=zipfile.ZIP_DEFLATED)
    for _, media, kind in GOBOS:
        z.writestr(f"wheels/{media}.png", gobo(kind), compress_type=zipfile.ZIP_STORED)
print("wrote", os.path.normpath(out))
