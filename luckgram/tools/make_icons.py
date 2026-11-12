#!/usr/bin/env python3
"""Regenerate Telegram/Resources/art icons from luckgram/branding/icon-1024.png.

Run from the repo root (needs Pillow):
  python3 luckgram/tools/make_icons.py luckgram/branding/icon-1024.png Telegram/Resources/art
"""
import sys, os
from PIL import Image, ImageDraw
src, art = sys.argv[1], sys.argv[2]
SS = 4  # supersampling for smooth edges

def shape(size, margin_ratio=0.0, round_shape=False):
    big = size * SS
    m = int(big * margin_ratio)
    inner = big - 2 * m
    img = Image.open(src).convert('RGBA').resize((inner, inner), Image.LANCZOS)
    mask = Image.new('L', (inner, inner), 0)
    d = ImageDraw.Draw(mask)
    if round_shape:
        d.ellipse((0, 0, inner - 1, inner - 1), fill=255)
    else:
        d.rounded_rectangle((0, 0, inner - 1, inner - 1), radius=int(inner * 0.225), fill=255)
    out = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    out.paste(img, (m, m), mask)
    # thin outline so the light icon stays visible on light backgrounds
    ol = ImageDraw.Draw(out)
    w = max(SS, int(big / 128))
    if round_shape:
        ol.ellipse((m, m, m + inner - 1, m + inner - 1), outline=(0, 0, 0, 40), width=w)
    else:
        ol.rounded_rectangle((m, m, m + inner - 1, m + inner - 1), radius=int(inner * 0.225), outline=(0, 0, 0, 40), width=w)
    return out.resize((size, size), Image.LANCZOS)

# Upstream icons have a small transparent margin (~6%).
for base in (16, 32, 48, 64, 128, 256, 512):
    shape(base, 0.06).save(os.path.join(art, f'icon{base}.png'))
    shape(base * 2, 0.06).save(os.path.join(art, f'icon{base}@2x.png'))
shape(256, 0.06).save(os.path.join(art, 'logo_256.png'))
shape(256, 0.0).save(os.path.join(art, 'logo_256_no_margin.png'))
shape(1024, 0.06, round_shape=True).save(os.path.join(art, 'icon_round512@2x.png'))
ico = shape(256, 0.06)
sizes = [(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (128, 128), (256, 256)]
ico.save(os.path.join(art, 'icon256.ico'), sizes=sizes)
print('ok')
