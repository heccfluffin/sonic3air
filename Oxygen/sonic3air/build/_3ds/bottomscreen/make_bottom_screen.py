#!/usr/bin/env python3
"""
Generates the 3DS bottom screen for the Sonic 3 A.I.R. 3DS port: a controls overview with touch buttons for a quick
save state: glossy buttons on a blue zig-zag stripe backdrop. All artwork is drawn from scratch here (no game art).

Output, next to this script:
  s3air_bottom_screen_<theme>.bin  One per theme (see THEMES), embedded into the game by CMakeLists.txt (format below)
  bottom_screen_preview.png  The screen as it looks by default
  bottom_screen_parts.png    All the parts that get swapped in at runtime (pressed / greyed-out buttons, status messages)
  bottom_screen_themes.png   All themes side by side

Format of s3air_bottom_screen_<theme>.bin (zlib-compressed as a whole), all numbers little endian:
  4 bytes  "S3BS"
  u16      version (1)
  u16      number of parts
  per part: u16 x, u16 y, u16 width, u16 height, u32 offset of its pixels (from the start of the uncompressed data)
  pixels:  RGB565, rows top to bottom, in screen orientation (320 x 240 for the full screen)
Part 0 is the full screen; the order of the other parts must match "Part" in N3DSBottomScreen.cpp.

Usage:  python3 make_bottom_screen.py      (needs Pillow: pip install pillow)
"""

import math
import os
import random
import struct
import zlib

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

W, H = 320, 240
S = 3                       # Supersampling factor, for smooth edges after downscaling
HERE = os.path.dirname(os.path.abspath(__file__))

FONT_CANDIDATES = [
	'/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
	'DejaVuSans-Bold.ttf',
	'arialbd.ttf',
]

# Touch areas (in screen pixels) - N3DSBottomScreen.cpp uses the same rectangles
SAVE_BOX   = (206, 40, 314, 108)
LOAD_BOX   = (206, 116, 314, 184)
STATUS_BOX = (206, 192, 314, 232)
STATS_BOX  = (258, 7, 312, 27)

# Status messages, in the order of the STATUS_* parts in N3DSBottomScreen.cpp
STATUS_MESSAGES = [
	('Save states work', 'in levels'),			# Default outside of levels (menus, title screen, data select)
	('Saving...', ''),
	('State saved!', ''),
	('Loading...', ''),
	('State loaded!', ''),
	('No state saved', 'for this game yet'),
	('Something went', 'wrong, see log'),
	('Only works', 'in levels'),
]
# Default while playing: which save file the state belongs to, and whether it has one. For "No Save" and save files 1-8,
# each with "has a state" and "no state yet".
for slot_name in ['No Save game'] + [f'Save file {i}' for i in range(1, 9)]:
	STATUS_MESSAGES.append((slot_name, 'has a state'))
	STATUS_MESSAGES.append((slot_name, 'no state yet'))


def font(size):
	for path in FONT_CANDIDATES:
		try:
			return ImageFont.truetype(path, int(size * S))
		except OSError:
			pass
	raise SystemExit('No bold TrueType font found, edit FONT_CANDIDATES')


def mix(a, b, t):
	return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def grey(rgb):
	v = int(0.3 * rgb[0] + 0.59 * rgb[1] + 0.11 * rgb[2])
	return mix((v, v, v), (150, 160, 175), 0.4)


def new_layer():
	return Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))


def shape_mask(shape, box, radius):
	m = Image.new('L', (W * S, H * S), 0)
	md = ImageDraw.Draw(m)
	if shape == 'circle':
		md.ellipse(box, fill=255)
	else:
		md.rounded_rectangle(box, radius=max(1, int(radius)), fill=255)
	return m


def composite_masked(img, layer, mask):
	"""Composites 'layer' onto 'img', limited to 'mask' (the mask scales the layer's own transparency)."""
	layer = layer.copy()
	layer.putalpha(ImageChops.multiply(layer.getchannel('A'), mask))
	img.alpha_composite(layer)


def fill_through(img, rgba, mask):
	"""Composites a flat color onto 'img' through 'mask' (keeps what's underneath where the color is translucent)."""
	composite_masked(img, Image.new('RGBA', img.size, rgba if len(rgba) == 4 else rgba + (255,)), mask)


def text(img, xy, string, size, fill, outline, width=1, anchor='lm', shadow=True):
	layer = new_layer()
	d = ImageDraw.Draw(layer)
	fnt = font(size)
	x, y = xy[0] * S, xy[1] * S
	if shadow:
		d.text((x + 0.6 * S, y + 1.2 * S), string, font=fnt, fill=(0, 30, 70, 110), anchor=anchor, stroke_width=int(width * S), stroke_fill=(0, 30, 70, 110))
	d.text((x, y), string, font=fnt, fill=fill, anchor=anchor, stroke_width=int(width * S), stroke_fill=outline)
	img.alpha_composite(layer)


# --- Glossy ("aero") shapes ---

def glossy(img, box, shape, rgb, radius=None, pressed=False):
	"""Draws a glossy shape onto 'img'. 'box' is in screen pixels, shape is 'circle' or 'rect' (rounded)."""
	x0, y0, x1, y1 = [v * S for v in box]
	if pressed:
		y0 += S
		y1 += S
	w, h = x1 - x0, y1 - y0
	r = (min(w, h) / 2) if radius is None else radius * S
	inset = int(1.5 * S)

	# Soft drop shadow (a neutral dark tone, so it doesn't tint anything blue)
	if not pressed:
		shadow = shape_mask(shape, (x0 + S, y0 + 2 * S, x1 + S, y1 + 2 * S), r).filter(ImageFilter.GaussianBlur(2 * S))
		fill_through(img, (10, 20, 40, 110), shadow)

	# Rim: a darker shade of the button's own color
	fill_through(img, mix(rgb, (0, 0, 0), 0.5) + (255,), shape_mask(shape, (x0, y0, x1, y1), r))

	# Body: vertical gradient, lighter at the bottom (light passing through the "glass")
	body = new_layer()
	bd = ImageDraw.Draw(body)
	dark, light = (0.35, 0.2) if pressed else (0.2, 0.3)
	for yy in range(y0, y1 + 1):
		t = (yy - y0) / max(1, h)
		bd.line((x0, yy, x1, yy), fill=mix(mix(rgb, (0, 0, 0), dark), mix(rgb, (255, 255, 255), light), t ** 1.4) + (255,))
	composite_masked(img, body, shape_mask(shape, (x0 + inset, y0 + inset, x1 - inset, y1 - inset), r - inset))

	# Top gloss: white fading out towards the middle, composited over the body (so the body's color stays underneath)
	gloss = new_layer()
	gd = ImageDraw.Draw(gloss)
	gy1 = y0 + int(h * 0.5)
	strength = 110 if pressed else 190
	for yy in range(y0, gy1 + 1):
		t = (yy - y0) / max(1, gy1 - y0)
		gd.line((x0, yy, x1, yy), fill=(255, 255, 255, int(strength * (1 - t) + 20)))
	ginset = int(min(w, h) * 0.1)
	gmask = shape_mask(shape, (x0 + ginset, y0 + int(2.5 * S), x1 - ginset, gy1), max(1, r - ginset))
	composite_masked(img, gloss, gmask)

	# Bottom glow
	glow = shape_mask(shape, (x0 + int(w * 0.22), y1 - int(h * 0.32), x1 - int(w * 0.22), y1 - int(1.5 * S)), r).filter(ImageFilter.GaussianBlur(2 * S))
	fill_through(img, (255, 255, 255, 80), glow)


def face_button(img, cx, cy, r, rgb, label, size=11):
	glossy(img, (cx - r, cy - r, cx + r, cy + r), 'circle', rgb)
	text(img, (cx, cy + 0.5), label, size, (255, 255, 255), mix(rgb, (0, 0, 0), 0.6), anchor='mm', shadow=False)


def pill(img, x, cy, w, h, rgb, label, size=8):
	glossy(img, (x, cy - h // 2, x + w, cy + h // 2), 'rect', rgb)
	text(img, (x + w / 2, cy + 0.5), label, size, (255, 255, 255), mix(rgb, (0, 0, 0), 0.6), anchor='mm', shadow=False)


def dpad(img, cx, cy, size, highlight=None):
	"""A d-pad as one cross-shaped piece: rim, gradient body, gloss, a dimple in the middle and an arrow on each arm.
	'highlight' ('down' etc.) colors that arm's arrow yellow."""
	col = (75, 85, 110)
	half = size * S / 2
	arm = size * S * 0.36 / 2		# Half the width of an arm
	x, y = cx * S, cy * S
	r = 1.2 * S

	def cross(inset=0, dx=0, dy=0):
		m = Image.new('L', (W * S, H * S), 0)
		md = ImageDraw.Draw(m)
		md.rounded_rectangle((x - arm + inset + dx, y - half + inset + dy, x + arm - inset + dx, y + half - inset + dy), radius=max(1, r - inset / 2), fill=255)
		md.rounded_rectangle((x - half + inset + dx, y - arm + inset + dy, x + half - inset + dx, y + arm - inset + dy), radius=max(1, r - inset / 2), fill=255)
		return m

	# Shadow and rim
	fill_through(img, (10, 20, 40, 110), cross(dx=S, dy=2 * S).filter(ImageFilter.GaussianBlur(2 * S)))
	fill_through(img, mix(col, (0, 0, 0), 0.5) + (255,), cross())

	# Body: vertical gradient over the whole cross
	body = new_layer()
	bd = ImageDraw.Draw(body)
	for yy in range(int(y - half), int(y + half) + 1):
		t = (yy - (y - half)) / (2 * half)
		bd.line((x - half, yy, x + half, yy), fill=mix(mix(col, (0, 0, 0), 0.15), mix(col, (255, 255, 255), 0.35), t ** 1.3) + (255,))
	composite_masked(img, body, cross(inset=int(1.5 * S)))

	# Gloss: upper half of the cross
	gloss = new_layer()
	gd = ImageDraw.Draw(gloss)
	for yy in range(int(y - half), int(y) + 1):
		t = (yy - (y - half)) / half
		gd.line((x - half, yy, x + half, yy), fill=(255, 255, 255, int(150 * (1 - t) + 25)))
	composite_masked(img, gloss, cross(inset=int(2.5 * S)))

	# Dimple in the middle
	dr = arm * 0.55
	layer = new_layer()
	ld = ImageDraw.Draw(layer)
	ld.ellipse((x - dr, y - dr, x + dr, y + dr), fill=mix(col, (0, 0, 0), 0.35) + (255,))
	ld.ellipse((x - dr + S * 0.8, y - dr + S * 0.8, x + dr - S * 0.2, y + dr - S * 0.2), fill=mix(col, (0, 0, 0), 0.1) + (255,))
	img.alpha_composite(layer)

	# Arrows on the arms
	layer = new_layer()
	ld = ImageDraw.Draw(layer)
	a = arm * 0.62			# Arrow size
	tip = half - arm * 0.35	# Distance of the arrow tip from the center
	for direction, (ux, uy) in (('up', (0, -1)), ('down', (0, 1)), ('left', (-1, 0)), ('right', (1, 0))):
		tx, ty = x + ux * tip, y + uy * tip
		bx, by = x + ux * (tip - a), y + uy * (tip - a)
		px, py = -uy * a * 0.8, ux * a * 0.8
		color = (255, 214, 0, 255) if direction == highlight else (225, 232, 245, 220)
		ld.polygon([(tx, ty), (bx + px, by + py), (bx - px, by - py)], fill=color)
	img.alpha_composite(layer)


def circle_pad(img, cx, cy, r):
	"""The 3DS Circle Pad seen from above: a matte grey cap with a raised rim and a shallow dip, on a black base."""
	x, y, R = cx * S, cy * S, r * S
	circle = lambda rad, dy=0: shape_mask('circle', (x - rad, y - rad + dy, x + rad, y + rad + dy), rad)

	def gradient(rad, top, bottom, dy=0):
		layer = new_layer()
		ld = ImageDraw.Draw(layer)
		for yy in range(int(y + dy - rad), int(y + dy + rad) + 1):
			t = (yy - (y + dy - rad)) / (2 * rad)
			ld.line((x - rad, yy, x + rad, yy), fill=mix(top, bottom, t) + (255,))
		return layer

	# Soft drop shadow, like the other buttons
	fill_through(img, (10, 20, 40, 110), circle(R, 2 * S).filter(ImageFilter.GaussianBlur(2 * S)))

	# Black base, showing a bit more below the cap (depth)
	fill_through(img, (22, 22, 28, 255), circle(R))

	# Cap with its rim: lit from the top
	cap = R - int(1.4 * S)
	lift = -int(0.8 * S)
	composite_masked(img, gradient(cap, (162, 164, 172), (104, 106, 114), lift), circle(cap, lift))

	# Shallow dip in the middle: the rim shades its top part, its bottom part catches the light
	dip = int(cap * 0.74)
	composite_masked(img, gradient(dip, (112, 114, 122), (146, 148, 156), lift), circle(dip, lift))


def glass_panel(img, box, radius=10, alpha=70):
	x0, y0, x1, y1 = [v * S for v in box]
	shadow = shape_mask('rect', (x0 + 2 * S, y0 + 3 * S, x1 + 2 * S, y1 + 3 * S), radius * S).filter(ImageFilter.GaussianBlur(3 * S))
	fill_through(img, (0, 40, 90, 70), shadow)
	fill_through(img, (255, 255, 255, alpha), shape_mask('rect', (x0, y0, x1, y1), radius * S))
	# Outline
	layer = new_layer()
	ImageDraw.Draw(layer).rounded_rectangle((x0, y0, x1, y1), radius=radius * S, outline=(255, 255, 255, 180), width=S)
	img.alpha_composite(layer)
	# Gloss on the upper part
	gloss = shape_mask('rect', (x0 + 3 * S, y0 + 2 * S, x1 - 3 * S, y0 + min(22, (box[3] - box[1]) // 3) * S), (radius - 2) * S).filter(ImageFilter.GaussianBlur(S))
	fill_through(img, (255, 255, 255, 55), gloss)


def bevel_box(img, box, fill, light, shadow, outline=(30, 25, 60), pressed=False, alpha=255):
	"""A flat, square, beveled box like in 16-bit era menus: light edge top / left, shadow edge bottom / right."""
	x0, y0, x1, y1 = box
	layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))		# Drawn at 1:1 for crisp pixel edges, then scaled up
	d = ImageDraw.Draw(layer)
	d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=outline + (255,))
	d.rectangle((x0 + 1, y0 + 1, x1 - 2, y1 - 2), fill=fill + (alpha,))
	top, bottom = (shadow, light) if pressed else (light, shadow)
	d.rectangle((x0 + 1, y0 + 1, x1 - 2, y0 + 2), fill=top + (255,))
	d.rectangle((x0 + 1, y0 + 1, x0 + 2, y1 - 2), fill=top + (255,))
	d.rectangle((x0 + 1, y1 - 3, x1 - 2, y1 - 2), fill=bottom + (255,))
	d.rectangle((x1 - 3, y0 + 1, x1 - 2, y1 - 2), fill=bottom + (255,))
	img.alpha_composite(layer.resize((W * S, H * S), Image.NEAREST))


def outline_box(img, box, fill, outline, alpha=255, inner=None):
	"""A flat box with a thin light outline, like in 16-bit era menus (drawn at 1:1 for crisp pixel edges)."""
	x0, y0, x1, y1 = box
	layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
	d = ImageDraw.Draw(layer)
	d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=fill + (alpha,))
	d.rectangle((x0, y0, x1 - 1, y1 - 1), outline=outline + (255,))
	if inner:
		d.rectangle((x0 + 1, y0 + 1, x1 - 2, y1 - 2), outline=inner + (255,))
	img.alpha_composite(layer.resize((W * S, H * S), Image.NEAREST))


def framed_box(img, box, inner_fill, inner_alpha):
	"""Thick light frame with a drop shadow to the bottom right, around a translucent tinted inside."""
	x0, y0, x1, y1 = box
	layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
	d = ImageDraw.Draw(layer)
	d.rectangle((x0 + 4, y0 + 4, x1 + 3, y1 + 3), fill=THEME['frame_shadow'] + (255,))		# Drop shadow
	d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=THEME['frame_light'] + (255,))				# Frame
	d.rectangle((x0 + 1, y0 + 1, x1 - 2, y1 - 2), outline=(255, 255, 255, 255))			# Bright inner edge
	d.rectangle((x0 + 3, y0 + 3, x1 - 4, y1 - 4), outline=THEME['frame_shadow'] + (255,))	# Line inside the frame
	img.alpha_composite(layer.resize((W * S, H * S), Image.NEAREST))
	inside = Image.new('RGBA', (W, H), (0, 0, 0, 0))
	ImageDraw.Draw(inside).rectangle((x0 + 4, y0 + 4, x1 - 5, y1 - 5), fill=inner_fill + (inner_alpha,))
	img.alpha_composite(inside.resize((W * S, H * S), Image.NEAREST))


def panel(img, box, radius=10, alpha=60):
	"""A panel in the theme's frame style: glass, retro (flat, with a thin light outline) or framed."""
	if THEME.get('frame') == 'framed':
		framed_box(img, box, THEME['panel'], 150)
		return
	if THEME.get('frame') == 'retro':
		outline_box(img, box, THEME['panel'], THEME['panel_outline'], alpha=215)
	else:
		glass_panel(img, box, radius=radius, alpha=alpha)


# --- Composition ---

# Bottom screen themes, each with its fixed number in "n3ds::shop::Theme" (N3DSShop.h); the game finds the artwork by the
# theme's id. The first one is the default (free); the others can be bought with Play Coins in the game's "3DS" menu.
# (Number 1 was "Sky Aero", removed; the number stays reserved so saved choices keep pointing at the right themes.)
THEMES = [
	{ 'id': 'blue',    'name': 'Blue Zig-Zag',    'style': 'zigzag', 'light': (34, 104, 214), 'dark': (12, 38, 122), 'shade': (0, 8, 40),   'save': (50, 175, 70), 'load': (30, 165, 235), 'outline': (20, 70, 140) },
	{ 'id': 'crimson', 'name': 'Crimson Zig-Zag', 'style': 'zigzag', 'light': (205, 45, 55),  'dark': (95, 12, 28),  'shade': (30, 0, 8),   'save': (230, 110, 20), 'load': (205, 45, 45), 'outline': (110, 15, 30) },
	{ 'id': 'gold',    'name': 'Golden Zig-Zag',  'style': 'zigzag', 'light': (240, 170, 30), 'dark': (160, 80, 10), 'shade': (50, 20, 0),  'save': (235, 175, 25), 'load': (150, 160, 180), 'outline': (130, 60, 0) },
	{ 'id': 'emerald', 'name': 'Emerald Zig-Zag', 'style': 'zigzag', 'light': (40, 175, 95),  'dark': (10, 85, 45),  'shade': (0, 30, 10),  'save': (190, 205, 30), 'load': (20, 150, 150), 'outline': (10, 90, 45) },
	{ 'id': 'marble',  'name': 'Retro Marble',    'style': 'marble', 'frame': 'retro', 'light': (166, 166, 228), 'dark': (96, 94, 172),
	  'panel': (92, 92, 166), 'panel_outline': (186, 186, 238), 'stats': (92, 92, 166), 'title': (255, 225, 40),
	  'save': (40, 160, 60), 'load': (110, 100, 190), 'outline': (36, 28, 84) },
	{ 'id': 'sea',     'name': 'Deep Sea',        'style': 'sea', 'light': (40, 110, 240), 'dark': (10, 18, 70),
	  'save': (30, 190, 170), 'load': (60, 120, 255), 'outline': (10, 30, 100) },
	{ 'id': 'blocks',  'name': 'Lavender Blocks', 'style': 'blocks', 'frame': 'framed', 'light': (222, 222, 250), 'dark': (150, 148, 214),
	  'bg_light': (214, 214, 246), 'bg_mid': (226, 226, 252), 'bg_highlight': (255, 255, 255), 'bg_shadow': (160, 158, 222),
	  'frame_light': (236, 236, 252), 'frame_shadow': (150, 148, 214), 'panel': (130, 126, 200), 'stats': (130, 126, 200),
	  'save': (80, 170, 120), 'load': (120, 110, 210), 'outline': (70, 60, 150) },
	{ 'id': 'city',    'name': 'Sky City',        'style': 'city', 'light': (96, 172, 236), 'dark': (30, 80, 180),
	  'save': (60, 175, 50), 'load': (40, 110, 220), 'outline': (20, 60, 130) },
	{ 'id': 'aurora',  'name': 'Aurora',          'style': 'gradient', 'corners': ((40, 150, 215), (150, 110, 210), (60, 130, 90), (120, 70, 150)),
	  'light': (120, 150, 215), 'dark': (60, 70, 140), 'save': (60, 180, 110), 'load': (110, 120, 230), 'outline': (40, 40, 110) },
	{ 'id': 'sunset',  'name': 'Sunset Glow',     'style': 'gradient', 'corners': ((255, 170, 80), (240, 110, 120), (200, 90, 140), (110, 60, 150)),
	  'light': (250, 160, 110), 'dark': (140, 70, 140), 'save': (240, 150, 40), 'load': (180, 80, 170), 'outline': (110, 40, 80) },
	{ 'id': 'night',   'name': 'Starry Night',    'style': 'night',  'light': (30, 45, 100),  'dark': (8, 12, 40),   'shade': (0, 0, 10),   'save': (175, 190, 215), 'load': (85, 70, 190), 'outline': (15, 25, 70) },
]
THEME = THEMES[0]		# The theme being generated
_background_cache = {}


def zigzag_background(light, dark, shade_color):
	"""Zig-zag stripes in a light and a dark tone, like a handheld game's menu backdrop."""
	stripe = 16				# Height of each stripe, in screen pixels
	half_period = 16		# Horizontal distance between a zig and a zag
	amplitude = 6			# How far the zig-zag goes up and down

	img = Image.new('RGBA', (W * S, H * S), dark + (255,))
	d = ImageDraw.Draw(img)

	def edge(y):
		# Points of a zig-zag line at height y, across the whole width
		points = []
		for i in range(-1, W // half_period + 2):
			x = i * half_period
			points.append((x * S, (y + (amplitude if i % 2 else -amplitude)) * S))
		return points

	for top in range(-2 * stripe, H + 2 * stripe, 2 * stripe):
		upper = edge(top)
		lower = edge(top + stripe)
		d.polygon(upper + lower[::-1], fill=light + (255,))
		# A thin bright line along the top edge of each light stripe, for a bit of shine
		d.line(upper, fill=mix(light, (255, 255, 255), 0.35) + (255,), width=S)

	# Vertical shading: darker at the top and bottom, brightest in the middle
	shade = new_layer()
	sd = ImageDraw.Draw(shade)
	for y in range(H * S):
		t = y / (H * S - 1)
		alpha = int(110 * (abs(t - 0.45) / 0.55) ** 1.5)
		sd.line((0, y, W * S, y), fill=shade_color + (alpha,))
	img.alpha_composite(shade)
	return img


def night_background(light, dark, shade_color):
	"""Deep indigo zig-zag stripes with twinkling stars."""
	img = zigzag_background(light, dark, shade_color)
	rng = random.Random(11)
	stars = new_layer()
	sd = ImageDraw.Draw(stars)
	for _ in range(40):
		x, y = rng.randint(0, W) * S, rng.randint(0, H) * S
		r = rng.choice([1, 1, 1, 2]) * S * 0.6
		sd.ellipse((x - r, y - r, x + r, y + r), fill=(255, 250, 220, rng.randint(150, 255)))
	for _ in range(7):		# A few bigger four-pointed sparkles
		x, y = rng.randint(0, W) * S, rng.randint(0, H) * S
		a, b = rng.randint(4, 7) * S, S * 0.9
		sd.polygon([(x, y - a), (x + b, y - b), (x + a, y), (x + b, y + b), (x, y + a), (x - b, y + b), (x - a, y), (x - b, y - b)], fill=(255, 245, 200, 230))
	img.alpha_composite(stars.filter(ImageFilter.GaussianBlur(0.4 * S)))
	return img


def marble_background(light, dark):
	"""Crinkled stone texture in three flat tones, small winding streaks repeating as a tile, like a 16-bit era menu
	backdrop (drawn from scratch)."""
	rng = random.Random(21)
	tile = 40		# Tile size in pixels (seamless)
	cells = 10
	values = [[rng.random() for _ in range(cells)] for _ in range(cells)]

	def noise(x, y, frequency):
		# Periodic value noise (wraps around at the tile size, so the tiles join seamlessly)
		fx, fy = (x / tile) * frequency, (y / tile) * frequency
		ix, iy = int(math.floor(fx)), int(math.floor(fy))
		tx, ty = fx - ix, fy - iy
		tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
		v = lambda a, b: values[(iy + b) % frequency][(ix + a) % frequency]
		return (v(0, 0) * (1 - tx) + v(1, 0) * tx) * (1 - ty) + (v(0, 1) * (1 - tx) + v(1, 1) * tx) * ty

	mid = mix(dark, light, 0.55)
	pattern = Image.new('RGBA', (tile, tile))
	px = pattern.load()
	for y in range(tile):
		for x in range(tile):
			# Streaks along the diagonal: fast changes across it, slow along it
			n = noise(x, y, 5) * 0.55 + noise(x, y, 10) * 0.45
			streak = math.sin(((x - y) * 0.45) + n * 9.0)
			if streak > 0.55:
				color = dark
			elif streak > -0.15:
				color = mid
			else:
				color = light
			px[x, y] = color + (255,)

	img = Image.new('RGBA', (W, H))
	for ty in range(0, H, tile):
		for tx in range(0, W, tile):
			img.paste(pattern, (tx, ty))
	return img.resize((W * S, H * S), Image.NEAREST)


def sea_background():
	"""Under the sea, in a 16-bit style with a limited palette: shimmering surface, deep water with distant mountains and
	rising bubbles, a dark seabed silhouette and a bright floor below (drawn from scratch)."""
	rng = random.Random(5)
	img = Image.new('RGBA', (W, H))
	px = img.load()
	surface, seabed_top, floor_top = 26, 128, 150

	def dither(t, a, b, x, y):
		return a if t + (BAYER4[y & 3][x & 3] / 16.0 - 0.5) * 0.5 < 0.5 else b

	for y in range(H):
		for x in range(W):
			if y < surface:
				# Surface: bright, shimmering bands
				wave = math.sin(x * 0.21 + math.sin(y * 0.9) * 2.0) + math.sin(x * 0.07 - y * 0.5)
				t = y / surface
				color = dither(t, (150, 210, 255), (40, 110, 240), x, y)
				if wave > 1.1 and y < surface - 4:
					color = (220, 245, 255)
			elif y < seabed_top:
				t = (y - surface) / (seabed_top - surface)
				color = dither(t * 1.1, (30, 80, 230), (15, 40, 170), x, y)
			elif y < floor_top:
				color = (10, 18, 70)
			else:
				t = (y - floor_top) / (H - floor_top)
				color = dither(t, (25, 75, 235), (20, 60, 215), x, y)
			px[x, y] = color + (255,)

	d = ImageDraw.Draw(img)
	# Distant mountains (lighter silhouette), then the dark seabed with rocks and pillars
	mountains = [(x, seabed_top - 18 - int(14 * abs(math.sin(x * 0.018 + 0.6)) + 6 * math.sin(x * 0.07))) for x in range(0, W + 8, 8)]
	d.polygon(mountains + [(W, seabed_top), (0, seabed_top)], fill=(25, 55, 170, 255))
	ridge = [(x, seabed_top - 6 - int(5 * math.sin(x * 0.05) + 3 * math.sin(x * 0.17))) for x in range(0, W + 4, 4)]
	d.polygon(ridge + [(W, floor_top), (0, floor_top)], fill=(10, 18, 70, 255))
	for x0 in range(12, W, 46):
		h = rng.randint(8, 16)
		x = x0 + rng.randint(-6, 6)
		d.rectangle((x, seabed_top - h, x + 5, seabed_top), fill=(14, 26, 90, 255))
		d.rectangle((x + 1, seabed_top - h, x + 1, seabed_top), fill=(40, 80, 170, 255))
	# Light line where the seabed meets the bright floor
	d.line((0, floor_top, W, floor_top), fill=(60, 120, 245, 255))
	# Rising bubbles in a few columns
	for cx in (40, 104, 170, 236, 290):
		for k in range(10):
			by = surface + 6 + k * 9 + rng.randint(-2, 2)
			bx = cx + int(3 * math.sin(k * 1.3)) + rng.randint(-1, 1)
			if by < seabed_top - 20:
				px[bx, by] = (190, 230, 255, 255)
				if k % 3 == 0:
					px[bx + 1, by] = (120, 180, 255, 255)
	return img.resize((W * S, H * S), Image.NEAREST)


def blocks_background(light, mid, highlight, shadow):
	"""Pale background with raised, interlocking stepped shapes (embossed relief), like a 16-bit era menu (drawn from scratch).
	Each shape is 2-3 grid rectangles joined into steps / L shapes; later shapes lie on top of earlier ones."""
	rng = random.Random(9)
	cell = 4
	img = Image.new('RGBA', (W, H), light + (255,))
	px = img.load()
	for _ in range(150):
		x, y = rng.randrange(-16, W, cell), rng.randrange(-8, H, cell)
		rects = [(x, y, rng.randint(3, 7) * cell, rng.randint(1, 3) * cell)]
		for _ in range(rng.randint(1, 2)):
			bx, by, bw, bh = rects[-1]
			# Next step: shifted right / down, overlapping the previous rectangle
			nx = bx + rng.randint(1, max(1, bw // cell - 1)) * cell
			ny = by + rng.choice((-1, 1)) * rng.randint(1, 2) * cell
			rects.append((nx, ny, rng.randint(2, 6) * cell, rng.randint(1, 3) * cell))
		inside = lambda px_, py_: any(rx <= px_ < rx + rw and ry <= py_ < ry + rh for rx, ry, rw, rh in rects)
		x0 = min(r[0] for r in rects); y0 = min(r[1] for r in rects)
		x1 = max(r[0] + r[2] for r in rects) + 2; y1 = max(r[1] + r[3] for r in rects) + 2
		for yy in range(max(0, y0), min(H, y1 + 1)):
			for xx in range(max(0, x0), min(W, x1 + 1)):
				if inside(xx, yy):
					edge_light = not inside(xx, yy - 1) or not inside(xx - 1, yy)
					px[xx, yy] = (highlight if edge_light else mid) + (255,)
				elif inside(xx, yy - 1) or inside(xx - 1, yy) or inside(xx, yy - 2) or inside(xx - 2, yy) or inside(xx - 1, yy - 1):
					px[xx, yy] = shadow + (255,)		# 2 pixel shadow below / right of the shape
	return img.resize((W * S, H * S), Image.NEAREST)


def city_background():
	"""A bright sky with puffy pixel clouds, a skyline of blue towers and ring lattices, and a leafy green ground with
	grey stone blocks, in a 16-bit style (drawn from scratch)."""
	rng = random.Random(17)
	img = Image.new('RGBA', (W, H), (96, 172, 236, 255))
	d = ImageDraw.Draw(img)
	px = img.load()
	horizon, ground = 116, 150

	# Clouds in a 16-bit style: rounded puffs on a flat bottom, white on top with light blue bands along the bottom
	def cloud(cx, cy, w):
		mask = Image.new('L', (W, H), 0)
		md = ImageDraw.Draw(mask)
		puffs = max(2, w // 9)
		for k in range(puffs):
			r = rng.randint(4, 7)
			x = cx - w // 2 + int((k + 0.5) * w / puffs)
			md.ellipse((x - r, cy - r - rng.randint(0, 3), x + r, cy + r), fill=255)
		md.rectangle((cx - w // 2, cy - 2, cx + w // 2, cy + 3), fill=255)
		mp = mask.load()
		for yy in range(max(0, cy - 14), min(H, cy + 4)):		# Flat bottom: nothing below cy + 3
			band = (255, 255, 255) if yy < cy - 1 else ((226, 238, 252) if yy < cy + 2 else (196, 222, 248))
			for xx in range(max(0, cx - w // 2 - 8), min(W, cx + w // 2 + 8)):
				if mp[xx, yy]:
					px[xx, yy] = band + (255,)

	# Place whole clouds: inside the screen, above the tower tops (the towers reach up to about 60 pixels above the
	# horizon), and not overlapping each other
	placed = []
	tries = 0
	while len(placed) < 7 and tries < 400:
		tries += 1
		w = rng.randint(24, 46)
		cx, cy = rng.randint(w // 2 + 10, W - w // 2 - 10), rng.randint(16, horizon - 66)
		if all(abs(cx - px_) > (w + pw) // 2 + 8 or abs(cy - py_) > 16 for px_, py_, pw in placed):
			placed.append((cx, cy, w))
	for (cx, cy, w) in placed:
		cloud(cx, cy, w)
	for _ in range(6):		# Thin streaks of cloud
		sx, sy, sw = rng.randint(0, W), rng.randint(20, 90), rng.randint(10, 26)
		d.line((sx, sy, sx + sw, sy), fill=(236, 244, 253, 255))
		d.line((sx + 3, sy + 1, sx + sw - 4, sy + 1), fill=(200, 224, 248, 255))

	blues = [(14, 40, 120), (30, 80, 180), (60, 130, 220), (130, 190, 245)]		# Dark to light
	# Ring lattice behind the towers: rows of octagon outlines
	def octagon(cx, cy, r):
		a = int(r * 0.42)
		return [(cx - a, cy - r), (cx + a, cy - r), (cx + r, cy - a), (cx + r, cy + a), (cx + a, cy + r), (cx - a, cy + r), (cx - r, cy + a), (cx - r, cy - a)]
	for row, y in enumerate((horizon - 26, horizon - 12)):
		for x in range(-6 + row * 9, W + 18, 18):
			d.polygon(octagon(x, y, 9), outline=blues[1] + (255,))
			d.polygon(octagon(x, y, 8), outline=blues[2] + (255,))

	# Towers: boxes with window grids, and round towers with vertical shading bands
	x = -4
	while x < W:
		w = rng.randint(10, 22)
		h = rng.randint(30, 62)
		top = horizon - h
		if rng.random() < 0.45:
			# Round tower: shading bands from dark (left) to light, dark right edge
			bands = [blues[0], blues[1], blues[2], blues[3], blues[2], blues[1], blues[0]]
			for k in range(w):
				band = bands[min(len(bands) - 1, k * len(bands) // w)]
				d.line((x + k, top + 2, x + k, horizon + 4), fill=band + (255,))
			d.ellipse((x, top - 1, x + w - 1, top + 4), fill=blues[3] + (255,))
			for yy in range(top + 8, horizon, 9):		# Spiral-like stripes
				d.line((x + 2, yy, x + w - 3, yy - 3), fill=blues[0] + (255,))
		else:
			d.rectangle((x, top, x + w - 1, horizon + 4), fill=blues[1] + (255,))
			d.rectangle((x, top, x + 1, horizon + 4), fill=blues[2] + (255,))
			d.rectangle((x + w - 2, top, x + w - 1, horizon + 4), fill=blues[0] + (255,))
			for yy in range(top + 3, horizon, 4):
				for xx in range(x + 3, x + w - 3, 3):
					px[xx, yy] = blues[3] + (255,) if 0 <= xx < W else None or blues[3] + (255,)
		if rng.random() < 0.5:		# Antenna
			ax = x + w // 2
			d.line((ax, top - rng.randint(5, 10), ax, top), fill=blues[0] + (255,))
		x += w + rng.randint(-3, 4)

	# Ground: rows of bush clumps in a 16-bit sprite style (dark outline, body, lit top, shine), drawn back to front so each
	# row overlaps the one behind. Back rows are darker and bluer (depth); each clump is a few puffs of different sizes.
	# Stone blocks lie between the rows, shaded like the bushes, with shadow below and some leaves hanging over them.
	d.rectangle((0, horizon, W, H), fill=(16, 64, 24, 255))
	clump_rng = random.Random(3)

	def green_set(depth):
		# depth 0 = front; further back = darker and a little bluer
		k = min(1.0, depth * 0.12)
		shade = lambda c: tuple(int(c[i] * (1 - 0.35 * k) + (0, 20, 60)[i] * 0.35 * k) for i in range(3))
		return [shade(c) for c in ((14, 64, 18), (48, 132, 30), (92, 182, 44), (150, 220, 86))]

	def bush_clump(cx, base, greens, size):
		outline, body, lit, shine = greens
		puffs = [(cx + clump_rng.randint(-size // 2, size // 2), base - clump_rng.randint(0, size // 2), clump_rng.randint(size - 2, size + 2))
				 for _ in range(clump_rng.randint(2, 4))]
		for (x, y, r) in puffs:
			d.ellipse((x - r - 1, y - r - 1, x + r + 1, y + r + 1), fill=outline + (255,))
		for (x, y, r) in puffs:
			d.ellipse((x - r, y - r, x + r, y + r), fill=body + (255,))
		for (x, y, r) in puffs:
			d.ellipse((x - r + 2, y - r + 1, x + r - 3, y - 1), fill=lit + (255,))
			if clump_rng.random() < 0.8:
				d.ellipse((x - r + 3, y - r + 2, x - r + 3 + max(2, r // 2), y - r + 4), fill=shine + (255,))
			for _ in range(3):		# Leaf shadows
				d.point((x + clump_rng.randint(-r + 2, r - 2), y + clump_rng.randint(0, r - 2)), fill=outline + (255,))

	def stone_block(bx, by, greens):
		bw, bh = 26, 7
		d.rectangle((bx + 1, by + bh + 1, bx + bw + 1, by + bh + 2), fill=greens[0] + (255,))		# Shadow on the bushes below
		d.rectangle((bx, by, bx + bw, by + bh), fill=(134, 146, 150, 255))
		d.line((bx + 1, by, bx + bw - 1, by), fill=(196, 206, 206, 255))								# Light top edge
		d.line((bx, by + 1, bx, by + bh - 1), fill=(172, 182, 184, 255))
		d.line((bx + 1, by + bh, bx + bw - 1, by + bh), fill=(64, 76, 84, 255))						# Dark bottom / right edge
		d.line((bx + bw, by + 1, bx + bw, by + bh - 1), fill=(64, 76, 84, 255))
		for k in range(bx + 6, bx + bw - 1, 7):															# Seams
			d.line((k, by + 2, k, by + bh - 2), fill=(96, 108, 114, 255))
		for _ in range(3):																				# Leaves hanging over the edge
			lx = bx + clump_rng.randint(1, bw - 3)
			d.rectangle((lx, by - 1, lx + clump_rng.randint(1, 3), by + clump_rng.randint(0, 1)), fill=greens[2] + (255,))

	rows = []
	y = horizon - 2
	while y < H + 14:
		rows.append(y)
		y += clump_rng.choice((10, 11, 12, 13))
	for row, base in enumerate(rows):
		greens = green_set(len(rows) - 1 - row)
		x = -10 + clump_rng.randint(0, 8)
		while x < W + 12:
			bush_clump(x, base, greens, clump_rng.choice((6, 7, 7, 8, 9)))
			x += clump_rng.randint(11, 19)
		if row % 2 == 1 and base < H - 6:
			for bx in range(-12 + (row // 2 % 2) * 42, W, 84):
				stone_block(bx + clump_rng.randint(-3, 3), base - 6, greens)
	return img.resize((W * S, H * S), Image.NEAREST)


def gradient_background(corners):
	"""A smooth gradient between four corner colors (top left, top right, bottom left, bottom right)."""
	tl, tr, bl, br = corners
	img = Image.new('RGBA', (W, H))
	px = img.load()
	for y in range(H):
		v = y / (H - 1)
		left, right = mix(tl, bl, v), mix(tr, br, v)
		for x in range(W):
			px[x, y] = mix(left, right, x / (W - 1)) + (255,)
	return img.resize((W * S, H * S), Image.BILINEAR)


def image_background(filename):
	"""A background picture drawn by hand: exactly 320 x 240 pixels, in the folder "themes" next to this script."""
	img = Image.open(os.path.join(HERE, 'themes', filename)).convert('RGBA')
	if img.size != (W, H):
		raise SystemExit(f"themes/{filename} must be exactly {W} x {H} pixels (it is {img.size[0]} x {img.size[1]})")
	return img.resize((W * S, H * S), Image.NEAREST)		# Pixel art stays crisp


def background():
	if THEME['style'] == 'overlay':
		return new_layer()		# Transparent: the game puts the player's own picture underneath
	key = THEME['id']
	if key not in _background_cache:
		if THEME['style'] == 'gradient':
			_background_cache[key] = gradient_background(THEME['corners'])
		elif THEME['style'] == 'city':
			_background_cache[key] = city_background()
		elif THEME['style'] == 'sea':
			_background_cache[key] = sea_background()
		elif THEME['style'] == 'blocks':
			_background_cache[key] = blocks_background(THEME['bg_light'], THEME['bg_mid'], THEME['bg_highlight'], THEME['bg_shadow'])
		elif THEME['style'] == 'marble':
			_background_cache[key] = marble_background(THEME['light'], THEME['dark'])
		elif THEME['style'] == 'image':
			_background_cache[key] = image_background(THEME['image'])
		elif THEME['style'] == 'night':
			_background_cache[key] = night_background(THEME['light'], THEME['dark'], THEME['shade'])
		else:
			_background_cache[key] = zigzag_background(THEME['light'], THEME['dark'], THEME['shade'])
	return _background_cache[key].copy()


STATS_COLOR = (95, 105, 130)


def big_button(img, box, rgb, word, state):
	"""The touch buttons: state is 'normal', 'pressed' or 'disabled'."""
	col = grey(rgb) if state == 'disabled' else rgb
	if THEME.get('frame') == 'framed':
		pressed_box = (box[0] + 1, box[1] + 1, box[2] + 1, box[3] + 1) if state == 'pressed' else box
		framed_box(img, pressed_box, mix(col, (0, 0, 0), 0.2) if state == 'pressed' else col, 255)
	elif THEME.get('frame') == 'retro':
		fill = mix(col, (0, 0, 0), 0.25) if state == 'pressed' else col
		outline_box(img, box, fill, THEME['panel_outline'], inner=mix(fill, (0, 0, 0), 0.35))
	else:
		glossy(img, box, 'rect', col, radius=14, pressed=(state == 'pressed'))
	cx = (box[0] + box[2]) / 2
	dy = 1 if state == 'pressed' else 0
	fg = (225, 230, 238) if state == 'disabled' else (255, 255, 255)
	text(img, (cx, box[1] + 27 + dy), word, 17, fg, mix(col, (0, 0, 0), 0.6), width=1.4, anchor='mm')
	text(img, (cx, box[1] + 47 + dy), 'STATE', 10, fg, mix(col, (0, 0, 0), 0.6), width=1, anchor='mm')


def stats_button(img, state):
	if THEME.get('frame') == 'framed':
		col = THEME.get('stats', STATS_COLOR)
		x0, y0, x1, y1 = STATS_BOX
		layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
		d = ImageDraw.Draw(layer)
		d.rectangle((x0 + 2, y0 + 2, x1 + 1, y1 + 1), fill=THEME['frame_shadow'] + (255,))
		d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=(mix(col, (0, 0, 0), 0.2) if state == 'pressed' else col) + (255,), outline=THEME['frame_light'] + (255,))
		img.alpha_composite(layer.resize((W * S, H * S), Image.NEAREST))
	elif THEME.get('frame') == 'retro':
		col = THEME.get('stats', STATS_COLOR)
		outline_box(img, STATS_BOX, mix(col, (0, 0, 0), 0.25) if state == 'pressed' else col, THEME['panel_outline'])
	else:
		glossy(img, STATS_BOX, 'rect', STATS_COLOR, pressed=(state == 'pressed'))
	dy = 1 if state == 'pressed' else 0
	text(img, ((STATS_BOX[0] + STATS_BOX[2]) / 2, (STATS_BOX[1] + STATS_BOX[3]) / 2 + 0.5 + dy), 'STATS', 8, (255, 255, 255), mix(STATS_COLOR, (0, 0, 0), 0.6), anchor='mm', shadow=False)


def status_text(img, lines):
	cx = (STATUS_BOX[0] + STATUS_BOX[2]) / 2
	cy = (STATUS_BOX[1] + STATUS_BOX[3]) / 2
	first, second = lines
	if second:
		text(img, (cx, cy - 6), first, 9, (255, 255, 255), THEME['outline'], anchor='mm')
		text(img, (cx, cy + 7), second, 9, (255, 255, 255), THEME['outline'], anchor='mm')
	else:
		text(img, (cx, cy + 0.5), first, 10, (255, 255, 255), THEME['outline'], anchor='mm')


def build(save='normal', load='normal', stats='normal', status=0):
	img = background()

	# Header: title on a glass bar, and the STATS button on the right
	panel(img, (6, 4, 314, 30), radius=9, alpha=60)
	text(img, (16, 17.5), 'CONTROLS', 14, THEME.get('title', (255, 255, 255)), THEME['outline'], width=1.4)
	stats_button(img, stats)

	# Controls list on a glass panel: icons on the left, action on the right
	panel(img, (6, 38, 198, 232), radius=10, alpha=55)
	label_x = 108
	rows_y = [55, 86, 117, 148, 179, 210]
	label = lambda y, s: text(img, (label_x, y), s, 12, (255, 255, 255), THEME['outline'], width=1.2)

	y = rows_y[0]
	circle_pad(img, 30, y, 11)
	dpad(img, 68, y, 24)
	label(y, 'MOVE')

	y = rows_y[1]
	for i, (lbl, col) in enumerate([('A', (225, 38, 46)), ('B', (240, 185, 20)), ('X', (40, 105, 225)), ('Y', (30, 165, 75))]):
		face_button(img, 24 + i * 22, y, 10, col, lbl, 10)
	label(y, 'JUMP')

	y = rows_y[2]
	dpad(img, 28, y, 22, highlight='down')
	text(img, (49, y), '+', 13, (255, 255, 255), THEME['outline'], anchor='mm')
	face_button(img, 70, y, 10, (225, 38, 46), 'A', 10)
	label(y, 'SPIN DASH')

	y = rows_y[3]
	pill(img, 14, y, 62, 16, (100, 108, 130), 'START')
	label(y, 'PAUSE')

	y = rows_y[4]
	pill(img, 14, y, 62, 16, (100, 108, 130), 'SELECT')
	label(y, 'BACK')

	y = rows_y[5]
	face_button(img, 22, y, 10, (225, 38, 46), 'A', 10)
	text(img, (35, y), 'OK', 8, (255, 255, 255), THEME['outline'])
	face_button(img, 64, y, 10, (240, 185, 20), 'B', 10)
	text(img, (77, y), 'BACK', 8, (255, 255, 255), THEME['outline'])
	label(y, 'MENUS')

	# Save state touch buttons and the status message below them
	big_button(img, SAVE_BOX, THEME['save'], 'SAVE', save)
	big_button(img, LOAD_BOX, THEME['load'], 'LOAD', load)
	panel(img, STATUS_BOX, radius=9, alpha=60)
	status_text(img, STATUS_MESSAGES[status])

	if THEME['style'] == 'overlay':
		return img.resize((W, H), Image.LANCZOS)		# Keeps the transparency
	return img.resize((W, H), Image.LANCZOS).convert('RGB')


# --- Output ---

BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def to_rgb565(img, box):
	"""Pixels of 'box' (screen coordinates) as RGB565, rows top to bottom. Ordered dithering against banding in the gradients,
	based on the screen position, so parts line up seamlessly with the full screen."""
	px = img.load()
	x0, y0, x1, y1 = box
	out = bytearray()
	for y in range(y0, y1):
		for x in range(x0, x1):
			r, g, b = px[x, y]
			t = BAYER4[y & 3][x & 3] / 16.0 - 0.5
			r = min(255, max(0, int(r + t * 8)))
			g = min(255, max(0, int(g + t * 4)))
			b = min(255, max(0, int(b + t * 8)))
			out += struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
	return bytes(out)


def build_theme():
	base = build(status=0)		# What the screen looks like before the game is running

	# Parts that get swapped in at runtime - order must match "Part" in N3DSBottomScreen.cpp
	parts = [(base, (0, 0, W, H))]
	for state in ('normal', 'pressed', 'disabled'):
		parts.append((build(save=state), SAVE_BOX))
	for state in ('normal', 'pressed', 'disabled'):
		parts.append((build(load=state), LOAD_BOX))
	for state in ('normal', 'pressed'):
		parts.append((build(stats=state), STATS_BOX))
	for index in range(len(STATUS_MESSAGES)):
		parts.append((build(status=index), STATUS_BOX))

	# The rectangles include a margin for shadows / the pressed offset
	def grow(box):
		if box == (0, 0, W, H):
			return box
		return (max(0, box[0] - 3), max(0, box[1] - 3), min(W, box[2] + 4), min(H, box[3] + 5))

	header = bytearray(b'S3BS') + struct.pack('<HH', 1, len(parts))
	data_offset = len(header) + 12 * len(parts)
	table = bytearray()
	pixels = bytearray()
	for img, box in parts:
		box = grow(box)
		table += struct.pack('<HHHHI', box[0], box[1], box[2] - box[0], box[3] - box[1], data_offset + len(pixels))
		pixels += to_rgb565(img, box)
	raw = bytes(header + table + pixels)
	with open(os.path.join(HERE, f"s3air_bottom_screen_{THEME['id']}.bin"), 'wb') as f:
		f.write(zlib.compress(raw, 9))
	if THEME is not THEMES[0]:
		return base

	base.save(os.path.join(HERE, 'bottom_screen_preview.png'))

	# Contact sheet of all parts
	sheet = Image.new("RGB", (W, H + 4 + 80 * 15), (40, 40, 40))
	sheet.paste(base, (0, 0))
	x, y = 0, H + 4
	for img, box in parts[1:]:
		box = grow(box)
		crop = img.crop(box)
		if x + crop.width > W:
			x, y = 0, y + 80
		sheet.paste(crop, (x, y))
		x += crop.width + 4
	sheet.save(os.path.join(HERE, 'bottom_screen_parts.png'))
	return base


# Overlay for the player's own background pictures (folder "themes" on the SD card): the panels, buttons and texts on a
# transparent background, with the colors of the default theme. The game puts it over the picture.
OVERLAY_THEME = dict(THEMES[0], id='overlay', style='overlay')


def build_overlay():
	"""s3air_bottom_screen_overlay.bin: like the theme files, but the pixels are RGBA (4 bytes, straight alpha)."""
	global THEME
	THEME = OVERLAY_THEME
	parts = [(build(status=0), (0, 0, W, H))]
	for state in ('normal', 'pressed', 'disabled'):
		parts.append((build(save=state), SAVE_BOX))
	for state in ('normal', 'pressed', 'disabled'):
		parts.append((build(load=state), LOAD_BOX))
	for state in ('normal', 'pressed'):
		parts.append((build(stats=state), STATS_BOX))
	for index in range(len(STATUS_MESSAGES)):
		parts.append((build(status=index), STATUS_BOX))

	def grow(box):
		if box == (0, 0, W, H):
			return box
		return (max(0, box[0] - 3), max(0, box[1] - 3), min(W, box[2] + 4), min(H, box[3] + 5))

	header = bytearray(b'S3BO') + struct.pack('<HH', 1, len(parts))
	data_offset = len(header) + 12 * len(parts)
	table = bytearray()
	pixels = bytearray()
	for img, box in parts:
		box = grow(box)
		table += struct.pack('<HHHHI', box[0], box[1], box[2] - box[0], box[3] - box[1], data_offset + len(pixels))
		pixels += img.crop(box).tobytes()		# RGBA, rows top to bottom
	with open(os.path.join(HERE, 's3air_bottom_screen_overlay.bin'), 'wb') as f:
		f.write(zlib.compress(bytes(header + table + pixels), 9))
	print('Wrote s3air_bottom_screen_overlay.bin')


def main():
	global THEME
	previews = []
	for theme in THEMES:
		THEME = theme
		previews.append(build_theme())
		print(f"Wrote s3air_bottom_screen_{theme['id']}.bin")

	# All themes side by side
	sheet = Image.new('RGB', (2 * W + 12, ((len(THEMES) + 1) // 2) * (H + 22) + 4), (30, 30, 30))
	d = ImageDraw.Draw(sheet)
	label_font = ImageFont.truetype(FONT_CANDIDATES[0], 13) if os.path.exists(FONT_CANDIDATES[0]) else None
	for i, (theme, img) in enumerate(zip(THEMES, previews)):
		x, y = 4 + (i % 2) * (W + 4), 4 + (i // 2) * (H + 22)
		d.text((x, y), theme['name'], fill=(255, 255, 255), font=label_font)
		sheet.paste(img, (x, y + 18))
	sheet.save(os.path.join(HERE, 'bottom_screen_themes.png'))
	build_overlay()
	print('Wrote bottom_screen_preview.png, bottom_screen_parts.png and bottom_screen_themes.png')


if __name__ == '__main__':
	main()
