#!/usr/bin/env python3
# Draws Lumora's icon: three sharp-edged cool white beams from one lens, with spill where they land.
# Writes resources/lumora.png, lumora.ico and the installer bitmaps wix-banner.bmp / wix-dialog.bmp.
#   python3 tools/make_icon.py [resources-dir] [preview.png]
from PIL import Image, ImageDraw, ImageFilter
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
RES = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'resources')
OUT = 1024
S = OUT * 2            # drawn at 2x, scaled down for clean antialiased edges
k = S / 1024
COOL = (214, 230, 255)
def L(): return Image.new('RGBA',(S,S),(0,0,0,0))
def make():
    im=L()
    box=[v*k for v in (40,40,984,984)]
    ImageDraw.Draw(im).rounded_rectangle(box,radius=200*k,fill=(12,14,19,255))
    cx,cy=512*k,190*k; floor=800*k
    d=ImageDraw.Draw(im)
    d.ellipse((cx-70*k,cy-42*k,cx+70*k,cy+38*k),fill=COOL+(255,))   # lens
    # spill: soft light on the floor around where each beam lands
    g=L(); gd=ImageDraw.Draw(g)
    for dx in (-230,0,230):
        gd.ellipse((cx+(dx-112)*k,floor-40*k,cx+(dx+112)*k,floor+40*k),fill=COOL+(80,))
    im.alpha_composite(g.filter(ImageFilter.GaussianBlur(18*k)))
    # sharp-edged beams, drawn over the lens; one layer so overlaps don't brighten
    b=L(); bd=ImageDraw.Draw(b)
    for dx in (-230,0,230):
        sx=cx+dx*0.2*k
        bd.polygon([(sx-24*k,cy),(sx+24*k,cy),(cx+(dx+72)*k,floor),(cx+(dx-72)*k,floor)],fill=COOL+(100,))
    for dx in (-230,0,230):
        bd.ellipse((cx+(dx-72)*k,floor-24*k,cx+(dx+72)*k,floor+24*k),fill=COOL+(100,))
    im.alpha_composite(b)
    mask=Image.new('L',(S,S),0); ImageDraw.Draw(mask).rounded_rectangle(box,radius=200*k,fill=255)
    out=L(); out.paste(im,(0,0),mask)
    return out.resize((OUT,OUT),Image.LANCZOS)

im = make()
im.resize((256, 256), Image.LANCZOS).save(os.path.join(RES, 'lumora.png'))
im.save(os.path.join(RES, 'lumora.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
if len(sys.argv) > 2:
    im.save(sys.argv[2])
ban = Image.new('RGB', (493, 58), (255, 255, 255))
i46 = im.resize((46, 46), Image.LANCZOS)
ban.paste(i46, (440, 6), i46)
ban.save(os.path.join(RES, 'wix-banner.bmp'))
dlg = Image.new('RGB', (493, 312), (255, 255, 255))
dd = ImageDraw.Draw(dlg)
dd.rectangle((0, 0, 163, 312), fill=(12, 14, 19))
dd.rectangle((0, 200, 163, 203), fill=COOL)
ic = im.resize((112, 112), Image.LANCZOS)
dlg.paste(ic, (26, 50), ic)
dlg.save(os.path.join(RES, 'wix-dialog.bmp'))
