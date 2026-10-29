# -*- coding: utf-8 -*-
"""Generate a horizontally-seamless cartoon natural-scenery image (440x384)."""
import math
import random
from PIL import Image, ImageDraw, ImageFilter

W, H = 440, 384
S = 4  # supersample factor for anti-aliasing
W2, H2 = W * S, H * S

random.seed(20261029)

def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))

def hexc(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))

img = Image.new("RGB", (W2, H2), (255, 255, 255))
d = ImageDraw.Draw(img, "RGBA")

# ---------- 1. Sky (vertical gradient only => horizontally seamless) ----------
top = hexc("#5eb8f0")
bottom = hexc("#eaf8ff")
for y in range(H2):
    t = y / H2
    d.line([(0, y), (W2, y)], fill=lerp(top, bottom, t))

# ---------- 2. Sun with soft glow (kept fully inside canvas => no seam) ----------
sun_cx, sun_cy, sun_r = int(W2 * 0.74), int(H2 * 0.26), int(W2 * 0.075)
glow = Image.new("RGBA", (W2, H2), (0, 0, 0, 0))
gd = ImageDraw.Draw(glow)
for rr in range(int(sun_r * 3.2), sun_r, -max(1, int(sun_r * 0.08))):
    a = int(30 * (1 - (rr - sun_r) / (sun_r * 2.2)))
    gd.ellipse([sun_cx - rr, sun_cy - rr, sun_cx + rr, sun_cy + rr],
               fill=(255, 238, 140, max(0, a)))
glow = glow.filter(ImageFilter.GaussianBlur(sun_r * 0.5))
img = Image.alpha_composite(img.convert("RGBA"), glow).convert("RGB")
d = ImageDraw.Draw(img, "RGBA")
d.ellipse([sun_cx - sun_r, sun_cy - sun_r, sun_cx + sun_r, sun_cy + sun_r],
          fill=(255, 214, 90))
d.ellipse([sun_cx - sun_r, sun_cy - sun_r, sun_cx + sun_r, sun_cy + sun_r],
          outline=(255, 190, 60), width=max(1, sun_r // 12))

# ---------- 3. Wrapped-draw helper for seamless periodic elements ----------
def draw_wrapped(draw_fn, px, half_extent):
    px0 = px % W2
    draw_fn(px0)
    if px0 + half_extent > W2:
        draw_fn(px0 - W2)
    if px0 - half_extent < 0:
        draw_fn(px0 + W2)

# ---------- 4. Clouds (periodic, wrapped) ----------
def make_cloud(cy):
    def draw_cloud(cx):
        s = int(H2 * 0.016)
        cloud = (255, 255, 255, 245)
        d.ellipse([cx - s*3, cy - s, cx + s*3, cy + s], fill=cloud)
        d.ellipse([cx - s*2, cy - s*2, cx + s, cy + s], fill=cloud)
        d.ellipse([cx - s, cy - s*3, cx + s*2, cy + s], fill=cloud)
        d.ellipse([cx + s, cy - s*2, cx + s*3, cy + s], fill=cloud)
    return draw_cloud

cloud_spacing = W2 / 3
for i in range(3):
    cx = int(cloud_spacing * i + cloud_spacing * 0.5 + math.sin(i * 2.1) * W2 * 0.02)
    cy = int(H2 * 0.18) + int(math.sin(i * 1.7) * H2 * 0.03)
    draw_wrapped(make_cloud(cy), cx, int(W2 * 0.12))

# ---------- 5. Birds (periodic, wrapped) ----------
def make_bird(by):
    def draw_bird(bx):
        w = int(H2 * 0.022)
        d.arc([bx - w, by - w, bx, by + w], 200, 340, fill=(70, 90, 110), width=max(2, w // 5))
        d.arc([bx, by - w, bx + w, by + w], 200, 340, fill=(70, 90, 110), width=max(2, w // 5))
    return draw_bird

for i in range(4):
    bx = int(W2 * (i / 4 + 0.08))
    by = int(H2 * (0.12 + 0.03 * math.sin(i * 2.3)))
    draw_wrapped(make_bird(by), bx, int(H2 * 0.03))

# ---------- 6. Layered rolling hills (sine waves, integer periods => seamless) ----------
def hill(base_y, amp, k, phase, color):
    pts = [(0, H2)]
    for x in range(0, W2 + 1):
        y = base_y - amp * (0.5 + 0.5 * math.sin(2 * math.pi * k * x / W2 + phase))
        pts.append((x, int(y)))
    pts.append((W2, H2))
    d.polygon(pts, fill=color)

hill(H2 * 0.62, H2 * 0.10, 2, 0.0, hexc("#a7d98a"))
hill(H2 * 0.72, H2 * 0.12, 3, 1.3, hexc("#7cc56f"))
hill(H2 * 0.84, H2 * 0.14, 4, 2.6, hexc("#57ad53"))

# ---------- 7. Cartoon trees (periodic, wrapped) ----------
def make_tree(ty, scale):
    def draw_tree(tx):
        trunk_h = int(H2 * 0.10 * scale)
        trunk_w = int(H2 * 0.018 * scale)
        d.rectangle([tx - trunk_w // 2, ty - trunk_h, tx + trunk_w // 2, ty],
                    fill=hexc("#8a5a34"))
        fr = int(H2 * 0.055 * scale)
        fcolor = hexc("#2f8f4c")
        fshade = hexc("#247a3d")
        d.ellipse([tx - fr, ty - trunk_h - fr, tx + fr, ty - trunk_h + fr], fill=fcolor)
        d.ellipse([tx - int(fr*0.85), ty - trunk_h - int(fr*1.7),
                   tx + int(fr*0.85), ty - trunk_h - int(fr*0.3)], fill=fshade)
        d.ellipse([tx - int(fr*0.6), ty - trunk_h - int(fr*2.4),
                   tx + int(fr*0.6), ty - trunk_h - int(fr*1.0)], fill=fcolor)
    return draw_tree

for i in range(5):
    tx = int(W2 * (i / 5 + 0.1))
    ty = int(H2 * (0.80 + 0.05 * math.sin(i * 1.9)))
    draw_wrapped(make_tree(ty, 0.9 + 0.25 * math.sin(i * 2.7)), tx, int(W2 * 0.10))

# ---------- 8. Grass tufts & flowers (periodic, wrapped) ----------
def make_grass(gy):
    def draw_grass(gx):
        gh = int(H2 * 0.028)
        for a in (-1, 0, 1):
            d.line([(gx, gy), (gx + a * gh // 3, gy - gh)],
                   fill=hexc("#3f9a3f"), width=max(2, gh // 8))
    return draw_grass

def make_flower(fy):
    def draw_flower(fx):
        r = int(H2 * 0.010)
        c = random.choice(["#ff6f91", "#ffc145", "#ffffff", "#ff8a5c"])
        for k in range(5):
            ang = k * 2 * math.pi / 5 - math.pi / 2
            px = fx + int(r * math.cos(ang))
            py = fy + int(r * math.sin(ang))
            d.ellipse([px - r, py - r, px + r, py + r], fill=hexc(c))
        d.ellipse([fx - r // 2, fy - r // 2, fx + r // 2, fy + r // 2], fill=hexc("#f4d03f"))
    return draw_flower

for i in range(9):
    gx = int(W2 * (i / 9 + 0.03))
    gy = int(H2 * (0.93 + 0.04 * math.sin(i * 3.1)))
    draw_wrapped(make_grass(gy), gx, int(H2 * 0.05))
for i in range(6):
    fx = int(W2 * (i / 6 + 0.07))
    fy = int(H2 * (0.95 + 0.03 * math.cos(i * 2.4)))
    draw_wrapped(make_flower(fy), fx, int(H2 * 0.02))

# ---------- 9. Downsample to final resolution ----------
img = img.resize((W, H), Image.LANCZOS)

out = r"E:\001\AIC-BarDisplay\cartoon_landscape_440x384.png"
img.save(out, "PNG")
print("saved:", out, img.size)

# ---------- 10. Seam check ----------
px = img.load()
diff = 0
for y in range(H):
    for c in range(3):
        diff += abs(px[0][y][c] - px[W - 1][y][c])
print("mean per-channel |left-right edge diff|:", diff / (H * 3))
