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
	glossy(img, (cx - r, cy - r, cx + r, cy + r), 'circle', (80, 88, 105))
	glossy(img, (cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3), 'circle', (150, 158, 175))


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


# --- Composition ---

# Bottom screen themes, each with its fixed number in "n3ds::shop::Theme" (N3DSShop.h); the game finds the artwork by the
# theme's id. The first one is the default (free); the others can be bought with Play Coins in the game's "3DS" menu.
# (Number 1 was "Sky Aero", removed; the number stays reserved so saved choices keep pointing at the right themes.)
THEMES = [
	{ 'id': 'blue',    'name': 'Blue Zig-Zag',    'style': 'zigzag', 'light': (34, 104, 214), 'dark': (12, 38, 122), 'shade': (0, 8, 40),   'save': (50, 175, 70), 'load': (30, 165, 235), 'outline': (20, 70, 140) },
	{ 'id': 'crimson', 'name': 'Crimson Zig-Zag', 'style': 'zigzag', 'light': (205, 45, 55),  'dark': (95, 12, 28),  'shade': (30, 0, 8),   'save': (230, 110, 20), 'load': (205, 45, 45), 'outline': (110, 15, 30) },
	{ 'id': 'gold',    'name': 'Golden Zig-Zag',  'style': 'zigzag', 'light': (240, 170, 30), 'dark': (160, 80, 10), 'shade': (50, 20, 0),  'save': (235, 175, 25), 'load': (150, 160, 180), 'outline': (130, 60, 0) },
	{ 'id': 'emerald', 'name': 'Emerald Zig-Zag', 'style': 'zigzag', 'light': (40, 175, 95),  'dark': (10, 85, 45),  'shade': (0, 30, 10),  'save': (190, 205, 30), 'load': (20, 150, 150), 'outline': (10, 90, 45) },
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


def background():
	key = THEME['id']
	if key not in _background_cache:
		if THEME['style'] == 'night':
			_background_cache[key] = night_background(THEME['light'], THEME['dark'], THEME['shade'])
		else:
			_background_cache[key] = zigzag_background(THEME['light'], THEME['dark'], THEME['shade'])
	return _background_cache[key].copy()


STATS_COLOR = (95, 105, 130)


def big_button(img, box, rgb, word, state):
	"""The touch buttons: state is 'normal', 'pressed' or 'disabled'."""
	col = grey(rgb) if state == 'disabled' else rgb
	glossy(img, box, 'rect', col, radius=14, pressed=(state == 'pressed'))
	cx = (box[0] + box[2]) / 2
	dy = 1 if state == 'pressed' else 0
	fg = (225, 230, 238) if state == 'disabled' else (255, 255, 255)
	text(img, (cx, box[1] + 27 + dy), word, 17, fg, mix(col, (0, 0, 0), 0.6), width=1.4, anchor='mm')
	text(img, (cx, box[1] + 47 + dy), 'STATE', 10, fg, mix(col, (0, 0, 0), 0.6), width=1, anchor='mm')


def stats_button(img, state):
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
	glass_panel(img, (6, 4, 314, 30), radius=9, alpha=60)
	text(img, (16, 17.5), 'CONTROLS', 14, (255, 255, 255), THEME['outline'], width=1.4)
	stats_button(img, stats)

	# Controls list on a glass panel: icons on the left, action on the right
	glass_panel(img, (6, 38, 198, 232), radius=10, alpha=55)
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
	glass_panel(img, STATUS_BOX, radius=9, alpha=60)
	status_text(img, STATUS_MESSAGES[status])

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


def main():
	global THEME
	previews = []
	for theme in THEMES:
		THEME = theme
		previews.append(build_theme())
		print(f"Wrote s3air_bottom_screen_{theme['id']}.bin")

	# All themes side by side
	sheet = Image.new('RGB', (2 * W + 12, 3 * (H + 22) + 4), (30, 30, 30))
	d = ImageDraw.Draw(sheet)
	label_font = ImageFont.truetype(FONT_CANDIDATES[0], 13) if os.path.exists(FONT_CANDIDATES[0]) else None
	for i, (theme, img) in enumerate(zip(THEMES, previews)):
		x, y = 4 + (i % 2) * (W + 4), 4 + (i // 2) * (H + 22)
		d.text((x, y), theme['name'], fill=(255, 255, 255), font=label_font)
		sheet.paste(img, (x, y + 18))
	sheet.save(os.path.join(HERE, 'bottom_screen_themes.png'))
	print('Wrote bottom_screen_preview.png, bottom_screen_parts.png and bottom_screen_themes.png')


if __name__ == '__main__':
	main()
