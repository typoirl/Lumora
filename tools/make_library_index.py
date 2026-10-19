#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the fixture-library indexes bundled with Lumora, so the library browser works offline.

    git clone --depth 1 https://github.com/OpenLightingProject/open-fixture-library ofl
    curl -O https://raw.githubusercontent.com/mcallegari/qlcplus/master/resources/fixtures/FixturesMap.xml
    tools/make_library_index.py ofl/fixtures FixturesMap.xml resources/library

The browser refreshes these from the internet when it can; the bundled copies are the fallback.
"""
import json
import os
import sys
import xml.etree.ElementTree as ET


def ofl_index(root):
    with open(os.path.join(root, "manufacturers.json"), encoding="utf-8") as f:
        makers = json.load(f)
    out = []
    for man in sorted(os.listdir(root)):
        folder = os.path.join(root, man)
        if not os.path.isdir(folder):
            continue
        for fn in sorted(os.listdir(folder)):
            if not fn.endswith(".json"):
                continue
            with open(os.path.join(folder, fn), encoding="utf-8") as f:
                d = json.load(f)
            if "redirectTo" in d:
                continue
            modes = []
            for m in d.get("modes", []):
                n = 0
                for c in m.get("channels", []):
                    if isinstance(c, dict):
                        rep = c.get("repeatFor")
                        k = len(rep) if isinstance(rep, list) else 0
                        n += k * len(c.get("templateChannels", []))
                    else:
                        n += 1
                modes.append(n)
            out.append({
                "m": man,
                "k": fn[:-5],
                "n": d.get("name", fn[:-5]),
                "c": d.get("categories", []),
                "ch": modes,
            })
    names = {k: v.get("name", k) for k, v in makers.items() if not k.startswith("$")}
    return {"manufacturers": names, "fixtures": out}


def qlc_index(path):
    ns = "{http://www.qlcplus.org/FixturesMap}"
    tree = ET.parse(path)
    out = []
    for m in tree.getroot().iter(ns + "M"):
        for f in m.iter(ns + "F"):
            out.append({"m": m.get("n"), "f": f.get("n"), "n": f.get("m")})
    return {"fixtures": out}


def main():
    ofl_root, qlc_map, dest = sys.argv[1:4]
    os.makedirs(dest, exist_ok=True)
    with open(os.path.join(dest, "ofl-index.json"), "w", encoding="utf-8") as f:
        json.dump(ofl_index(ofl_root), f, separators=(",", ":"), ensure_ascii=False)
    with open(os.path.join(dest, "qlcplus-index.json"), "w", encoding="utf-8") as f:
        json.dump(qlc_index(qlc_map), f, separators=(",", ":"), ensure_ascii=False)


if __name__ == "__main__":
    main()
