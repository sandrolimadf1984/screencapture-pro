"""Gera os ícones do programa (app.ico / rec.ico e as versões .png de 256 px).

Uso: python3 make_assets.py <pasta_assets>   (precisa do Pillow: pip install pillow)
"""
import os, sys
from PIL import Image, ImageDraw

OUT = sys.argv[1]

def draw_icon(size, dot_color):
    S = size * 8  # desenha grande e reduz (bordas suaves)
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pad = int(S * 0.04)
    r = int(S * 0.22)
    # fundo azul com degradê vertical
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    top, bot = (59, 130, 246), (29, 78, 216)
    for y in range(S):
        t = y / (S - 1)
        c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,)
        gd.line([(0, y), (S, y)], fill=c)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([pad, pad, S - pad, S - pad], radius=r, fill=255)
    im.paste(grad, (0, 0), mask)
    # monitor branco
    lw = max(int(S * 0.065), 2)
    mx0, my0, mx1, my1 = int(S * 0.20), int(S * 0.27), int(S * 0.80), int(S * 0.66)
    d.rounded_rectangle([mx0, my0, mx1, my1], radius=int(S * 0.05), outline=(255, 255, 255, 255), width=lw)
    d.line([(S // 2, my1), (S // 2, int(S * 0.76))], fill=(255, 255, 255, 255), width=lw)
    d.line([(int(S * 0.36), int(S * 0.78)), (int(S * 0.64), int(S * 0.78))], fill=(255, 255, 255, 255), width=lw)
    # ponto de gravação no canto
    cx, cy, rr = int(S * 0.76), int(S * 0.25), int(S * 0.15)
    d.ellipse([cx - rr - lw // 2, cy - rr - lw // 2, cx + rr + lw // 2, cy + rr + lw // 2], fill=(17, 21, 28, 255))
    d.ellipse([cx - rr + lw // 3, cy - rr + lw // 3, cx + rr - lw // 3, cy + rr - lw // 3], fill=dot_color)
    return im.resize((size, size), Image.LANCZOS)

sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
for name, color in (("app", (239, 68, 68, 255)), ("rec", (34, 197, 94, 255))):
    imgs = [draw_icon(s, color) for s in sizes]
    imgs[-1].save(os.path.join(OUT, name + ".ico"), sizes=[(s, s) for s in sizes], append_images=imgs[:-1])
    imgs[-1].save(os.path.join(OUT, name + "_256.png"))
print("icones ok")
