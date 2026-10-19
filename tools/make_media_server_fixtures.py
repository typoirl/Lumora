#!/usr/bin/env python3
"""Writes Lumora's built-in media/laser server fixtures (OFL JSON) into fixtures/.

Pangolin BEYOND DMX Server: the Classic FB3 map (Basic 4 / Standard 8 / Extended 12 / Full 16
channels, chosen by the Access channel) and the Mid (32) and Max (50) maps, from
https://wiki.pangolin.com/beyond:dmx_server

Resolume Arena: the DMX Auto Map (composition 27 channels, each layer 38 channels: layer n starts
at 28 + 38 x (n-1)), from https://resolume.com/download/Resolume_Arena_4_DMX_Auto_Map_Sheet.pdf.
Arena 6 and 7 load it as a DMX shortcut preset.
"""
import json, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "fixtures")


def generic(comment, default=None, fine=None):
    ch = {"capability": {"type": "Generic", "comment": comment}}
    if default is not None:
        ch["defaultValue"] = default
    if fine:
        ch["fineChannelAliases"] = [fine]
    return ch


def ranges(rows, default=None):
    ch = {"capabilities": [{"dmxRange": [a, b], "type": t, "comment": c} if t != "NoFunction"
                           else {"dmxRange": [a, b], "type": t} for a, b, t, c in rows]}
    if default is not None:
        ch["defaultValue"] = default
    return ch


def toggle(name):
    return ranges([(0, 127, "Generic", "Off"), (128, 255, "Generic", name + " on")])


def event(name):
    return ranges([(0, 127, "NoFunction", ""), (128, 255, "Generic", name)])


def write(path, data):
    path = os.path.join(ROOT, path)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    print("wrote", os.path.relpath(path, ROOT))


# ---- Pangolin BEYOND -----------------------------------------------------------------------
def access(default, label):
    return ranges([(0, 31, "Generic", "Blackout"), (32, 96, "Generic", "Basic: 4 channels"),
                   (97, 160, "Generic", "Standard: 8 channels"), (161, 224, "Generic", "Extended: 12 channels"),
                   (225, 255, "Generic", "Full: 16 channels")], default)

fb3 = {
    "Access 4ch": access(64, "Basic"),
    "Access 8ch": access(128, "Standard"),
    "Access 12ch": access(192, "Extended"),
    "Access": access(255, "Full"),
    "Page": ranges([(i * 16, i * 16 + 15, "Generic", "Page %d" % (i + 1)) for i in range(9)] +
                   [(144, 255, "Generic", "Page 9")], 0),
    "Cue": ranges([(0, 31, "Generic", "Blackout")] +
                  [(32 + i * 4, 35 + i * 4, "Generic", "Cue %d" % (i + 1)) for i in range(48)] +
                  [(224, 255, "Generic", "Cue 48")], 0),
    "Speed": ranges([(0, 15, "Generic", "100%"), (16, 31, "NoFunction", ""),
                     (32, 255, "Generic", "25% to 200% (127 = 100%)")], 0),
    "Brightness": {"defaultValue": 255, "capability": {"type": "Intensity"}},
    "Zoom": generic("0 = zero size, 255 = full size", 255),
    "Size X": generic("Negative full size to positive full size", 255),
    "Size Y": generic("Negative full size to positive full size", 255),
    "Angle Z": generic("0 to 360 degrees", 0),
    "Position X": generic("Left, 127 center, right", 127),
    "Position Y": generic("Down, 127 center, top", 127),
    "Visible points": generic("None to all", 255),
    "Scan rate": ranges([(0, 31, "Generic", "Default"), (32, 255, "Generic", "25% to 200% (127 = 100%)")], 0),
    "Release": ranges([(0, 255, "NoFunction", "")], 0),
    "Color slider": ranges([(0, 31, "Generic", "Original colors"),
                            (32, 224, "Generic", "Hue: red, yellow, lime, cyan, blue, purple, pink, red"),
                            (225, 255, "Generic", "White")], 0),
    "Reserved": ranges([(0, 255, "NoFunction", "")], 0),
}
fb3_order = ["Page", "Cue", "Speed", "Brightness", "Zoom", "Size X", "Size Y", "Angle Z", "Position X",
             "Position Y", "Visible points", "Scan rate", "Release", "Color slider", "Reserved"]
write("pangolin/beyond-dmx-server-fb3.json", {
    "name": "BEYOND DMX Server (FB3)",
    "shortName": "BEYOND FB3",
    "manufacturer": "Pangolin",
    "categories": ["Laser"],
    "meta": {"source": "Pangolin wiki: BEYOND DMX Server, Classic FB3 style mode"},
    "availableChannels": fb3,
    "modes": [
        {"name": "Basic", "shortName": "4ch", "channels": ["Access 4ch"] + fb3_order[:3]},
        {"name": "Standard", "shortName": "8ch", "channels": ["Access 8ch"] + fb3_order[:7]},
        {"name": "Extended", "shortName": "12ch", "channels": ["Access 12ch"] + fb3_order[:11]},
        {"name": "Full", "shortName": "16ch", "channels": ["Access"] + fb3_order},
    ],
})

speed = ranges([(0, 127, "Generic", "0 to 100%"), (128, 255, "Generic", "100% to 500%")], 127)
mid = {
    "Page": ranges([(0, 0, "Generic", "Off"), (1, 255, "Generic", "Page 1, 2, 3…")], 0),
    "Cue": ranges([(0, 0, "Generic", "Off"), (1, 255, "Generic", "Cue 1 to 255")], 0),
    "Animation speed": ranges([(0, 127, "Generic", "0 to 100%"), (128, 255, "Generic", "100% to 500%")], 255),
    "Animation mode": ranges([(0, 255, "NoFunction", "")], 0),
    "Scan rate": generic("25% to 100%", 255),
    "Visible points": generic("0 to 100%", 255),
    "Size X": generic("-100% to 100% (middle: size 0)", "100%", "Size X fine"),
    "Size Y": generic("-100% to 100% (middle: size 0)", "100%", "Size Y fine"),
    "Angle Z": generic("0 to 360 degrees", 0, "Angle Z fine"),
    "Position X": generic("-100% to 100%, middle centered", "50%", "Position X fine"),
    "Position Y": generic("-100% to 100%, middle centered", "50%", "Position Y fine"),
    "Zoom": generic("0 to 100%", "100%", "Zoom fine"),
    "Color slider": ranges([(0, 32, "Generic", "Inactive"), (33, 224, "Generic", "Hue"),
                            (225, 255, "Generic", "White")], 0),
    "Brightness": {"defaultValue": 255, "capability": {"type": "Intensity"}},
    "Hue shift": generic("0 to 360 degrees", 0),
    "Saturation shift": generic("-100% to 100% (128 = none)", 128),
    "Red": {"defaultValue": 0, "capability": {"type": "ColorIntensity", "color": "Red"}},
    "Green": {"defaultValue": 0, "capability": {"type": "ColorIntensity", "color": "Green"}},
    "Blue": {"defaultValue": 0, "capability": {"type": "ColorIntensity", "color": "Blue"}},
    "Alpha": ranges([(0, 0, "Generic", "No recolor"), (1, 255, "Generic", "Recolor to R, G, B")], 0),
}
for n in range(1, 5):
    mid["FX %d" % n] = ranges([(0, 0, "Generic", "Off"), (1, 255, "Generic", "FX index")], 0)
    mid["FX %d action" % n] = generic("Not applied to full", 255)
    mid["FX %d speed" % n] = speed
for n in range(1, 13):
    mid["Param %d" % n] = generic("0 to 100%", 0)
head = ["Page", "Cue", "Animation speed", "Animation mode", "Scan rate", "Visible points",
        "Size X", "Size X fine", "Size Y", "Size Y fine", "Angle Z", "Angle Z fine",
        "Position X", "Position X fine", "Position Y", "Position Y fine", "Zoom", "Zoom fine",
        "Color slider", "Brightness", "Hue shift", "Saturation shift", "Red", "Green", "Blue", "Alpha"]
mid_mode = head + ["FX %d" % n for n in range(1, 5)] + ["Param 1", "Param 2"]
max_mode = head + sum([["FX %d" % n, "FX %d action" % n, "FX %d speed" % n] for n in range(1, 5)], []) + \
    ["Param %d" % n for n in range(1, 13)]
assert len(mid_mode) == 32 and len(max_mode) == 50
write("pangolin/beyond-dmx-server.json", {
    "name": "BEYOND DMX Server",
    "shortName": "BEYOND",
    "manufacturer": "Pangolin",
    "categories": ["Laser"],
    "meta": {"source": "Pangolin wiki: BEYOND DMX Server, Mid and Max modes"},
    "availableChannels": mid,
    "modes": [
        {"name": "Mid", "shortName": "32ch", "channels": mid_mode},
        {"name": "Max", "shortName": "50ch", "channels": max_mode},
    ],
})

# ---- Resolume Arena ------------------------------------------------------------------------
def select(options, default=0):
    rows = []
    n = len(options)
    for i, o in enumerate(options):
        rows.append((i, i if i < n - 1 else 255, "Generic", o))
    return ranges(rows, default)

direction = select(["Backwards", "Forwards", "Paused", "Random"], 1)
comp = {
    "Master": {"defaultValue": 255, "capability": {"type": "Intensity"}},
    "Bypass": toggle("Bypass"),
    "Clear": event("Clear"),
    "Crossfader": generic("A to B", 128),
    "Trigger column": generic("Columns 1 to 256", 0),
    "Select deck": ranges([(0, 0, "NoFunction", ""), (1, 255, "Generic", "Deck 1 to 255")], 0),
}
for n in range(1, 9):
    comp["Dashboard dial %d" % n] = generic("0 to 1", 0)
comp.update({
    "BPM": generic("2 to 500 BPM (60 = 120 BPM)", 60),
    "Tap": event("Tap"),
    "Resync": event("Resync"),
    "Pause": toggle("Pause"),
    "Master speed": generic("0 to 10 (25 = 1)", 25),
    "Master direction": direction,
    "Volume": generic("-40 dB to +12 dB (196 = 0 dB)", 196),
    "Pan": generic("Left to right (128 = center)", 128),
    "Fade out": generic("0 to 1", 255),
    "Scale": generic("0 to 1000% (26 = 100%)", 26),
    "Rotate X": generic("-180 to 180 degrees", 127),
    "Rotate Y": generic("-180 to 180 degrees", 127),
    "Rotate Z": generic("-180 to 180 degrees", 127),
})
assert len(comp) == 27
write("resolume/arena-composition.json", {
    "name": "Arena Composition",
    "shortName": "Arena comp",
    "manufacturer": "Resolume",
    "categories": ["Other"],
    "meta": {"source": "Resolume Arena DMX Auto Map: composition, channels 1-27"},
    "availableChannels": comp,
    "modes": [{"name": "Auto map", "shortName": "27ch", "channels": list(comp)}],
})

layer = {
    "AV fader": {"defaultValue": 255, "capability": {"type": "Intensity"}},
    "Bypass": toggle("Bypass"),
    "Clear": event("Clear"),
    "Solo": toggle("Solo"),
    "Bus": select(["Off", "Bus A", "Bus B"]),
    "Move up": event("Move up"),
    "Move down": event("Move down"),
    "Trigger clip": generic("Clips 1 to 256", 0),
}
for n in range(1, 9):
    layer["Dashboard dial %d" % n] = generic("0 to 1", 0)
layer.update({
    "Playhead": generic("Start to end", 0),
    "Play mode": select(["Loop", "Bounce", "Play once", "Play once and hold"]),
    "Direction": direction,
    "Speed": generic("0 to 10 (25 = 1)", 25),
    "Transition time": generic("0 to 10 s", 0),
    "Transition blend": generic("Blend mode index", 4),
    "Auto pilot": select(["Play next", "Play previous", "Play random", "Paused"], 3),
    "Volume": generic("0 to 1", 255),
    "Pan": generic("Left to right (128 = center)", 128),
    "Opacity": generic("0 to 1", 255),
    "Blend mode": generic("Blend mode index (4 = Alpha)", 4),
    "Width": generic("0 to 4096 (enable it in Arena first)", 0),
    "Height": generic("0 to 4096 (enable it in Arena first)", 0),
    "Scale": generic("0 to 1000% (26 = 100%)", 26),
    "Position X": generic("-4096 to 4096 (128 = 0)", 128),
    "Position Y": generic("-4096 to 4096 (128 = 0)", 128),
    "Rotate X": generic("-180 to 180 degrees", 127),
    "Rotate Y": generic("-180 to 180 degrees", 127),
    "Rotate Z": generic("-180 to 180 degrees", 127),
    "Anchor X": generic("-2048 to 2048 (128 = 0)", 128),
    "Anchor Y": generic("-2048 to 2048 (128 = 0)", 128),
    "Anchor Z": generic("-2048 to 2048 (128 = 0)", 128),
})
assert len(layer) == 38, len(layer)
write("resolume/arena-layer.json", {
    "name": "Arena Layer",
    "shortName": "Arena layer",
    "manufacturer": "Resolume",
    "categories": ["Other"],
    "meta": {"source": "Resolume Arena DMX Auto Map: one layer, 38 channels (layer n at 28 + 38 x (n-1))"},
    "availableChannels": layer,
    "modes": [{"name": "Auto map", "shortName": "38ch", "channels": list(layer)}],
})
