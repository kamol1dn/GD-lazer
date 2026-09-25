# Renders logo.png (336x336, the size Geode's index shows): a GD cube in the
# mod's purple-pink, inside a ring of visualiser bars. Drawn at 4x and scaled
# down for smooth edges. Run from the repo root: python tools/make_logo.py
import math
import random

from PIL import Image, ImageDraw, ImageFilter

SIZE = 336
S = 4  # supersampling
W = SIZE * S
C = W / 2

PURPLE = (102, 68, 204)
PINK = (255, 102, 170)
DARK = (24, 20, 34)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


img = Image.new('RGBA', (W, W), (0, 0, 0, 0))

# Background: dark rounded square with a soft purple glow in the middle.
bg = Image.new('RGBA', (W, W), (0, 0, 0, 0))
ImageDraw.Draw(bg).rounded_rectangle([0, 0, W - 1, W - 1], radius=int(W * 0.22), fill=DARK + (255,))
glow = Image.new('RGBA', (W, W), (0, 0, 0, 0))
ImageDraw.Draw(glow).ellipse([C - W * 0.36, C - W * 0.36, C + W * 0.36, C + W * 0.36], fill=PURPLE + (150,))
glow = glow.filter(ImageFilter.GaussianBlur(W * 0.09))
mask = bg.split()[3]
bg.alpha_composite(Image.composite(glow, Image.new('RGBA', (W, W)), mask))
img.alpha_composite(bg)

# Visualiser: bars radiating from a ring, fading from purple to pink round the circle.
bars = Image.new('RGBA', (W, W), (0, 0, 0, 0))
d = ImageDraw.Draw(bars)
random.seed(7)
count = 64
inner = W * 0.30
for i in range(count):
    a = -math.pi / 2 + 2 * math.pi * i / count
    # a smooth "spectrum": low-frequency hump plus some jitter
    h = W * (0.035 + 0.075 * (0.5 + 0.5 * math.sin(i * 0.9)) * (0.6 + 0.4 * random.random()))
    t = i / count
    col = lerp(PURPLE, PINK, 0.5 - 0.5 * math.cos(2 * math.pi * t)) + (230,)
    x0, y0 = C + math.cos(a) * inner, C + math.sin(a) * inner
    x1, y1 = C + math.cos(a) * (inner + h), C + math.sin(a) * (inner + h)
    d.line([x0, y0, x1, y1], fill=col, width=int(W * 0.018))
img.alpha_composite(bars)

# The cube: rounded outer square with a gradient fill, dark inner square, light inner core.
cube = W * 0.36
half = cube / 2
grad = Image.new('RGBA', (W, W), (0, 0, 0, 0))
gd = ImageDraw.Draw(grad)
for y in range(int(C - half), int(C + half) + 1):
    t = (y - (C - half)) / cube
    gd.line([C - half, y, C + half, y], fill=lerp(PINK, PURPLE, t) + (255,))
cube_mask = Image.new('L', (W, W), 0)
ImageDraw.Draw(cube_mask).rounded_rectangle([C - half, C - half, C + half, C + half], radius=int(cube * 0.14), fill=255)
shadow = Image.new('RGBA', (W, W), (0, 0, 0, 0))
ImageDraw.Draw(shadow).rounded_rectangle([C - half, C - half + W * 0.02, C + half, C + half + W * 0.02],
                                         radius=int(cube * 0.14), fill=(0, 0, 0, 140))
img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(W * 0.02)))
img.alpha_composite(Image.composite(grad, Image.new('RGBA', (W, W)), cube_mask))

d = ImageDraw.Draw(img)
border = W * 0.022
d.rounded_rectangle([C - half, C - half, C + half, C + half], radius=int(cube * 0.14), outline=(255, 255, 255, 255),
                    width=int(border))
mid = cube * 0.46 / 2
d.rounded_rectangle([C - mid, C - mid, C + mid, C + mid], radius=int(mid * 0.22), fill=DARK + (235,))
core = cube * 0.22 / 2
d.rounded_rectangle([C - core, C - core, C + core, C + core], radius=int(core * 0.25), fill=(255, 255, 255, 255))

img.resize((SIZE, SIZE), Image.LANCZOS).save('logo.png')
print('wrote logo.png')
