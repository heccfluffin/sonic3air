#!/usr/bin/env python3
"""
Generates the pixel art icons of the bottom screen themes for the game's "3DS" menu (64 x 48 pixels each).
Each icon uses only the theme's two colors (its light and dark tone), plus transparent corners, so the game paints them in
the theme's colors. All drawn from scratch here, no game art.

Output:
  ../../../source/sonic3air/menu/N3DSThemeIcons.inc   Icon data for the game (2 bits per pixel: 0 = dark, 1 = light, 2 = transparent)
  theme_icons_preview.png                              What they look like (3x enlarged), in each theme's colors

Usage:  python3 make_theme_icons.py      (needs Pillow: pip install pillow)
"""

import math
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_INC = os.path.normpath(os.path.join(HERE, '..', '..', '..', 'source', 'sonic3air', 'menu', 'N3DSThemeIcons.inc'))
W, H = 64, 48
DARK, LIGHT, CLEAR = 0, 1, 2

# Same themes and colors as THEMES in make_bottom_screen.py (light, dark), with their numbers in n3ds::shop::Theme
# (number 1 was "Sky Aero", removed)
THEMES = [
	('blue',    0, (34, 104, 214),  (12, 38, 122)),
	('crimson', 2, (205, 45, 55),   (95, 12, 28)),
	('gold',    3, (240, 170, 30),  (160, 80, 10)),
	('emerald', 4, (40, 175, 95),   (10, 85, 45)),
	('night',   5, (175, 190, 215), (8, 12, 40)),
	('marble',  6, (166, 166, 228), (96, 94, 172)),
	('sea',     7, (60, 130, 245),  (10, 18, 70)),
	('blocks',  8, (236, 236, 252), (150, 148, 214)),
	('city',    9, (130, 200, 250), (96, 56, 160)),
	('aurora', 10, (110, 200, 230), (90, 60, 150)),
	('sunset', 11, (255, 180, 90),  (130, 60, 140)),
	('casino', 12, (90, 170, 255),  (8, 10, 40)),
]
NUM_THEME_NUMBERS = 15

# Themes made from a picture get a full color icon instead: the picture itself, scaled down (theme number, picture in themes/)
# and the part of it to show (x, y, width, height - 4:3 like the icon), so the zone is recognizable at this small size
COLOR_ICONS = [
	(12, 'casino.png', (4, 100, 192, 144)),		# The casino with its wheel, the pillars and the city lights behind
	(13, 'collision.png', (60, 70, 200, 150)),	# Collision Chaos: the city with its waterfalls below the green hills
	(14, 'metallic.png', (0, 40, 200, 150)),	# Metallic Madness: the steel blocks with palm trees and spring poles
]


def color_icon(filename, crop):
	"""The theme's picture (a part of it) scaled down to the icon size, with the same rounded corners as the other icons (RGBA)."""
	picture = Image.open(os.path.join(HERE, 'themes', filename)).convert('RGB')
	x, y, w, h = crop
	icon = picture.crop((x, y, x + w, y + h)).resize((W, H), Image.LANCZOS).convert('RGBA')
	corners = new_icon(LIGHT)[0]
	frame(corners)
	px, cp = icon.load(), corners.load()
	for y in range(H):
		for x in range(W):
			if cp[x, y] == CLEAR:
				px[x, y] = (0, 0, 0, 0)
	return icon


def new_icon(fill):
	img = Image.new('L', (W, H), fill)
	return img, ImageDraw.Draw(img)


def frame(img):
	"""Rounded corners (transparent) and a 1 pixel dark border, like a little sprite panel."""
	px = img.load()
	corner = [3, 1, 1]		# Transparent pixels per row in each corner, from the outside in
	for i, n in enumerate(corner):
		for x in range(n):
			for (cx, cy) in ((x, i), (W - 1 - x, i), (x, H - 1 - i), (W - 1 - x, H - 1 - i)):
				px[cx, cy] = CLEAR
	for x in range(W):
		for y in range(H):
			if px[x, y] == CLEAR:
				continue
			edge = x == 0 or y == 0 or x == W - 1 or y == H - 1
			near_corner = any(px[nx, ny] == CLEAR for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)) if 0 <= nx < W and 0 <= ny < H)
			if edge or near_corner:
				px[x, y] = DARK


def zigzag(img, stripe=6, amplitude=3, period=12):
	"""Light zig-zag stripes on dark, with crisp 1 pixel steps."""
	px = img.load()
	for x in range(W):
		phase = x % period
		offset = phase if phase <= period // 2 else period - phase		# Triangle wave 0 .. period/2
		offset = offset * amplitude * 2 // period
		for y in range(H):
			if ((y + offset + 1) // stripe) % 2 == 0:
				px[x, y] = LIGHT


def badge(d, cx, cy, r):
	"""Dark round badge with a light rim, to put an emblem on."""
	d.ellipse((cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1), fill=LIGHT)
	d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=DARK)


def emblem_ring(d, cx, cy):
	d.ellipse((cx - 9, cy - 9, cx + 9, cy + 9), fill=LIGHT)
	d.ellipse((cx - 5, cy - 6, cx + 5, cy + 6), fill=DARK)


def emblem_flame(d, cx, cy):
	# A flame: rounded bottom, three tongues at the top
	d.ellipse((cx - 8, cy - 2, cx + 8, cy + 10), fill=LIGHT)
	d.polygon([(cx - 8, cy + 3), (cx - 6, cy - 6), (cx - 3, cy - 1), (cx, cy - 11), (cx + 3, cy - 1), (cx + 6, cy - 6), (cx + 8, cy + 3)], fill=LIGHT)
	d.ellipse((cx - 3, cy + 2, cx + 3, cy + 8), fill=DARK)
	d.polygon([(cx - 3, cy + 4), (cx, cy - 3), (cx + 3, cy + 4)], fill=DARK)


def emblem_gem(d, cx, cy):
	# A cut gem: flat top, pointed bottom, with facet lines
	top, mid, bottom = cy - 8, cy - 3, cy + 10
	d.polygon([(cx - 6, top), (cx + 6, top), (cx + 11, mid), (cx, bottom), (cx - 11, mid)], fill=LIGHT)
	d.line([(cx - 11, mid), (cx + 11, mid)], fill=DARK)
	d.line([(cx - 3, top), (cx - 5, mid), (cx, bottom)], fill=DARK)
	d.line([(cx + 3, top), (cx + 5, mid), (cx, bottom)], fill=DARK)


def icon_zigzag(emblem=None):
	img, d = new_icon(DARK)
	zigzag(img)
	if emblem:
		badge(d, W // 2, H // 2, 15)
		emblem(d, W // 2, H // 2)
	frame(img)
	return img


def icon_night():
	img, d = new_icon(DARK)
	# Crescent moon
	d.ellipse((38, 6, 56, 24), fill=LIGHT)
	d.ellipse((33, 3, 51, 21), fill=DARK)
	# Sparkles (four-pointed) and small stars
	def sparkle(cx, cy, size):
		d.line((cx - size, cy, cx + size, cy), fill=LIGHT)
		d.line((cx, cy - size, cx, cy + size), fill=LIGHT)
		d.point([(cx - 1, cy - 1), (cx + 1, cy - 1), (cx - 1, cy + 1), (cx + 1, cy + 1)], fill=LIGHT)
	sparkle(14, 12, 4)
	sparkle(24, 32, 3)
	sparkle(48, 36, 4)
	for (x, y) in ((8, 28), (30, 8), (36, 22), (10, 40), (56, 30), (40, 42), (20, 22), (58, 8)):
		d.point((x, y), fill=LIGHT)
	# Zig-zag line along the bottom, like the theme's stripes
	for x in range(W):
		phase = x % 12
		offset = (phase if phase <= 6 else 12 - phase) // 2
		d.point((x, 44 - offset), fill=LIGHT)
	frame(img)
	return img


def icon_marble():
	"""Marble veins and a little beveled panel, in two tones."""
	img, d = new_icon(DARK)
	px = img.load()
	for y in range(H):
		for x in range(W):
			t = math.sin(x * 0.12 + y * 0.18 + math.sin(x * 0.07) * 2.0 + math.sin(y * 0.11) * 1.5)
			if t > 0.35:
				px[x, y] = LIGHT
	# Beveled panel in the middle: light top / left edge, dark bottom / right edge
	d.rectangle((14, 14, 49, 33), fill=DARK)
	d.rectangle((15, 15, 48, 32), fill=LIGHT)
	d.rectangle((17, 17, 48, 32), fill=DARK)
	d.rectangle((17, 17, 46, 30), fill=LIGHT)
	d.rectangle((19, 19, 46, 30), fill=DARK)
	for (x, y, w) in ((22, 22, 18), (22, 26, 12)):		# Two "text lines"
		d.rectangle((x, y, x + w, y + 1), fill=LIGHT)
	frame(img)
	return img


def icon_sea():
	"""Wavy water lines with a badge of rising bubbles."""
	img, d = new_icon(DARK)
	px = img.load()
	for x in range(W):
		for row in range(0, H + 8, 8):
			y = row + int(round(2 * math.sin(x * 2 * math.pi / 16)))
			for t in range(2):
				if 0 <= y + t < H:
					px[x, y + t] = LIGHT
	badge(d, W // 2, H // 2, 15)
	for (cx, cy, r) in ((W // 2 - 3, H // 2 + 4, 5), (W // 2 + 5, H // 2 - 3, 3), (W // 2 - 2, H // 2 - 8, 2)):
		d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=LIGHT)
		d.point((cx - r // 2, cy - r // 2), fill=LIGHT)
	frame(img)
	return img


def icon_blocks():
	"""Light stair-shaped blocks with a dark shadow, on a dithered mid-tone background (two colors only)."""
	import random
	rng = random.Random(13)
	img, d = new_icon(LIGHT)
	px = img.load()
	cell, shadow = 4, 2
	for y in range(H):
		for x in range(W):
			px[x, y] = DARK if (x + y) % 2 == 0 else LIGHT		# Dithered mid tone
	for _ in range(24):
		x, y = rng.randrange(-12, W, cell), rng.randrange(-8, H, cell)
		rects = []
		direction = rng.choice((-1, 1))
		for step in range(rng.randint(2, 3)):		# Stairs: each step shifted right and up / down by one cell
			rects.append((x, y, rng.randint(2, 4) * cell, rng.randint(1, 2) * cell))
			x += rng.randint(1, 2) * cell
			y += direction * cell
		inside = lambda x_, y_, rects=rects: any(rx <= x_ < rx + rw and ry <= y_ < ry + rh for rx, ry, rw, rh in rects)
		offsets = [(dx, dy) for dx in range(shadow + 1) for dy in range(shadow + 1) if dx or dy]
		for yy in range(H):
			for xx in range(W):
				if inside(xx, yy):
					px[xx, yy] = LIGHT
				elif any(inside(xx - dx, yy - dy) for dx, dy in offsets):
					px[xx, yy] = DARK
	frame(img)
	return img


def icon_city():
	"""Sky with two clouds above purple honeycomb towers and a domed building, bushes below."""
	img, d = new_icon(LIGHT)
	px = img.load()
	for (cx, cy) in ((14, 8), (48, 7)):		# Clouds: two puffs on a flat bottom, with a closed dark outline
		for grow, color in ((1, DARK), (0, LIGHT)):
			d.ellipse((cx - 7 - grow, cy - 3 - grow, cx + 1 + grow, cy + 4 + grow), fill=color)
			d.ellipse((cx - 2 - grow, cy - 5 - grow, cx + 7 + grow, cy + 4 + grow), fill=color)
			d.rectangle((cx - 7 - grow, cy + 1, cx + 7 + grow, cy + 4 + grow), fill=color)

	def hexagon(cx, cy, r):
		return [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))) for a in (-90, -30, 30, 90, 150, 210)]

	# Honeycomb towers: stacked hexagon cells (dark, with a light dot in each)
	for (x0, rows) in ((3, 4), (38, 4)):
		for row in range(rows):
			cy = 35 - row * 4.5
			for col in range(2):
				cx = x0 + 3 + col * 5.2 + (2.6 if row % 2 else 0)
				d.polygon(hexagon(cx, cy, 3), fill=DARK)
				px[int(cx), int(cy)] = LIGHT
	# Domed building with windows
	d.rectangle((20, 22, 31, 38), fill=DARK)
	d.pieslice((20, 16, 31, 28), 180, 360, fill=DARK)
	for yy in range(25, 37, 3):
		for xx in range(22, 30, 3):
			px[xx, yy] = LIGHT
	d.rectangle((52, 26, 60, 38), fill=DARK)
	for yy in range(28, 37, 3):
		px[54, yy] = LIGHT
		px[57, yy] = LIGHT
	# Bushes along the bottom: light bumps on dark
	d.rectangle((0, 38, W, H), fill=DARK)
	for x in range(0, W, 5):
		d.ellipse((x - 2, 40 + (x // 5) % 2 * 2, x + 3, 44 + (x // 5) % 2 * 2), fill=LIGHT)
	frame(img)
	return img


def icon_gradient():
	"""A smooth diagonal blend between the two colors (ordered dithering), with a little glass panel."""
	img, d = new_icon(DARK)
	px = img.load()
	bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
	for y in range(H):
		for x in range(W):
			t = 1.0 - (x / (W - 1) * 0.6 + y / (H - 1) * 0.4)		# Light at the top left
			if t * 16 > bayer[y & 3][x & 3] + 0.5:
				px[x, y] = LIGHT
	# Glass panel: light outline with a light top edge band
	d.rectangle((14, 12, 49, 35), outline=LIGHT)
	d.line((16, 14, 47, 14), fill=LIGHT)
	d.line((16, 15, 47, 15), fill=DARK)
	frame(img)
	return img


def icon_casino():
	"""Night sky with stars over a casino with a big round wheel in its middle (the theme's picture was made by the port's author)."""
	img, d = new_icon(DARK)
	for x, y in ((8, 6), (20, 10), (44, 5), (56, 12), (32, 4), (12, 16)):
		d.point((x, y), fill=LIGHT)
	d.rectangle((8, 26, 55, 45), outline=LIGHT)			# The building
	d.line((8, 22, 55, 22), fill=LIGHT)						# Its roof line with lights
	for x in range(10, 54, 4):
		d.point((x, 24), fill=LIGHT)
	d.rectangle((4, 20, 7, 45), fill=LIGHT)					# Pillars on both sides
	d.rectangle((56, 20, 59, 45), fill=LIGHT)
	d.ellipse((24, 28, 39, 43), outline=LIGHT)				# The wheel
	d.ellipse((29, 33, 34, 38), fill=LIGHT)
	for x in range(12, 22, 3):								# Windows
		for y in range(30, 44, 4):
			d.point((x, y), fill=LIGHT)
			d.point((x + 30, y), fill=LIGHT)
	frame(img)
	return img


ICONS = {
	'blue': lambda: icon_zigzag(),
	'crimson': lambda: icon_zigzag(emblem_flame),
	'gold': lambda: icon_zigzag(emblem_ring),
	'emerald': lambda: icon_zigzag(emblem_gem),
	'night': icon_night,
	'marble': icon_marble,
	'sea': icon_sea,
	'blocks': icon_blocks,
	'city': icon_city,
	'aurora': icon_gradient,
	'sunset': icon_gradient,
	'casino': icon_casino,
}


def main():
	icons = [ICONS[theme[0]]() for theme in THEMES]

	# Game data: 2 bits per pixel, 4 pixels per byte, rows top to bottom
	lines = ['// Generated by build/_3ds/bottomscreen/make_theme_icons.py - do not edit',
			 f'// {len(icons)} icons of {W} x {H} pixels, 2 bits per pixel (0 = dark, 1 = light, 2 = transparent), 4 pixels per byte',
			 f'static const int THEME_ICON_WIDTH = {W};',
			 f'static const int THEME_ICON_HEIGHT = {H};',
			 '// Icon for each theme number (n3ds::shop::Theme), -1 if there is none',
			 'static const int THEME_ICON_FOR_THEME[' + str(NUM_THEME_NUMBERS) + '] = { ' +
			 ', '.join(str(next((k for k, t in enumerate(THEMES) if t[1] == n), -1)) for n in range(NUM_THEME_NUMBERS)) + ' };',
			 f'static const unsigned char THEME_ICONS[{len(icons)}][{W * H // 4}] =', '{']
	for (name, _, _, _), img in zip(THEMES, icons):
		px = img.load()
		data = []
		for y in range(H):
			for x in range(0, W, 4):
				byte = 0
				for k in range(4):
					byte |= (px[x + k, y] & 3) << (k * 2)
				data.append(byte)
		lines.append(f'\t// {name}')
		lines.append('\t{')
		for i in range(0, len(data), 32):
			lines.append('\t\t' + ', '.join(f'0x{v:02x}' for v in data[i:i + 32]) + ',')
		lines.append('\t},')
	lines.append('};')

	# Full color icons: RGBA, rows top to bottom
	colorIcons = [color_icon(filename, crop) for _, filename, crop in COLOR_ICONS]
	lines.append('// Full color icon for each theme number (index in THEME_COLOR_ICONS), -1 if there is none: these replace the two color icon')
	lines.append('static const int THEME_COLOR_ICON_FOR_THEME[' + str(NUM_THEME_NUMBERS) + '] = { ' +
				 ', '.join(str(next((k for k, c in enumerate(COLOR_ICONS) if c[0] == n), -1)) for n in range(NUM_THEME_NUMBERS)) + ' };')
	lines.append(f'static const unsigned char THEME_COLOR_ICONS[{max(len(colorIcons), 1)}][{W * H * 4}] =')
	lines.append('{')
	for (number, filename, _), icon in zip(COLOR_ICONS, colorIcons):
		data = icon.tobytes()
		lines.append(f'\t// {filename}')
		lines.append('\t{')
		for i in range(0, len(data), 32):
			lines.append('\t\t' + ', '.join(f'0x{v:02x}' for v in data[i:i + 32]) + ',')
		lines.append('\t},')
	if not colorIcons:
		lines.append('\t{ 0 },')
	lines.append('};')
	for (number, filename, _), icon in zip(COLOR_ICONS, colorIcons):
		icon.resize((W * 3, H * 3), Image.NEAREST).save(os.path.join(HERE, 'theme_icon_' + filename))

	with open(OUT_INC, 'w') as f:
		f.write('\n'.join(lines) + '\n')

	# Preview in the theme colors, 3x enlarged
	scale = 3
	preview = Image.new('RGB', (len(icons) * (W + 6) * scale, (H + 6) * scale), (40, 40, 40))
	for i, ((name, _, light, dark), img) in enumerate(zip(THEMES, icons)):
		colored = Image.new('RGB', (W, H), (40, 40, 40))
		cp, ip = colored.load(), img.load()
		for y in range(H):
			for x in range(W):
				v = ip[x, y]
				if v != CLEAR:
					cp[x, y] = light if v == LIGHT else dark
		preview.paste(colored.resize((W * scale, H * scale), Image.NEAREST), ((i * (W + 6) + 3) * scale, 3 * scale))
	preview.save(os.path.join(HERE, 'theme_icons_preview.png'))
	print(f'Wrote {OUT_INC} and theme_icons_preview.png')


if __name__ == '__main__':
	main()
