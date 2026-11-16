#!/usr/bin/env python3
"""
Generates the 3DS bottom screen controls image for the Sonic 3 A.I.R. 3DS port.

Original artwork in a retro 16-bit style with glossy "aero" buttons (no characters or logos).
Outputs, next to this script:
  - controls.png : preview at the real size (320x240)
  - controls.bin : RGB565, pre-rotated for the 3DS bottom screen framebuffer (240x320, column-major), 153600 bytes

Needs Python 3 with Pillow:  python3 make_controls_screen.py
"""

import os
import struct
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 320, 240
S = 3                       # Supersampling factor, for smooth edges after downscaling
HERE = os.path.dirname(os.path.abspath(__file__))

FONT_CANDIDATES = [
	'/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
	'DejaVuSans-Bold.ttf',
	'arialbd.ttf',
]


def font(size):
	for path in FONT_CANDIDATES:
		try:
			return ImageFont.truetype(path, size * S)
		except OSError:
			pass
	raise SystemExit('No bold TrueType font found, edit FONT_CANDIDATES')


def mix(a, b, t):
	return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def outlined_text(draw, xy, text, fnt, fill, outline=(8, 20, 70), width=2, anchor='lm'):
	x, y = xy[0] * S, xy[1] * S
	draw.text((x + S, y + 2 * S), text, font=fnt, fill=(0, 0, 0, 140), anchor=anchor, stroke_width=width * S, stroke_fill=(0, 0, 0, 140))
	draw.text((x, y), text, font=fnt, fill=fill, anchor=anchor, stroke_width=width * S, stroke_fill=outline)


# --- Glossy ("aero") shapes ---

def glossy(base, size, shape, rgb, radius=None):
	"""Draws a glossy shape onto 'base' at a box given in output pixels: shape 'circle' or 'pill'."""
	x0, y0, x1, y1 = [v * S for v in size]
	w, h = x1 - x0, y1 - y0
	r = (min(w, h) // 2) if radius is None else radius * S
	layer = Image.new('RGBA', base.size, (0, 0, 0, 0))

	def mask(inset=0, box=None):
		m = Image.new('L', base.size, 0)
		md = ImageDraw.Draw(m)
		bx0, by0, bx1, by1 = box if box else (x0 + inset, y0 + inset, x1 - inset, y1 - inset)
		if shape == 'circle':
			md.ellipse((bx0, by0, bx1, by1), fill=255)
		else:
			md.rounded_rectangle((bx0, by0, bx1, by1), radius=max(1, r - inset), fill=255)
		return m

	# Soft drop shadow
	shadow = mask(box=(x0 + 1 * S, y0 + 3 * S, x1 + 1 * S, y1 + 3 * S)).filter(ImageFilter.GaussianBlur(3 * S))
	layer.paste((0, 0, 30, 150), (0, 0), shadow)

	# Dark rim
	layer.paste(mix(rgb, (0, 0, 0), 0.55) + (255,), (0, 0), mask())

	# Body: vertical gradient, lighter at the bottom (light passing through the "glass")
	body = Image.new('RGBA', base.size, (0, 0, 0, 0))
	bd = ImageDraw.Draw(body)
	for yy in range(y0, y1 + 1):
		t = (yy - y0) / max(1, h)
		col = mix(mix(rgb, (0, 0, 0), 0.25), mix(rgb, (255, 255, 255), 0.25), t ** 1.4)
		bd.line((x0, yy, x1, yy), fill=col + (255,))
	layer.paste(body, (0, 0), mask(inset=int(1.5 * S)))

	# Top gloss: white, fading out towards the middle
	gloss = Image.new('RGBA', base.size, (0, 0, 0, 0))
	gd = ImageDraw.Draw(gloss)
	gy1 = y0 + int(h * 0.52)
	for yy in range(y0, gy1 + 1):
		t = (yy - y0) / max(1, gy1 - y0)
		gd.line((x0, yy, x1, yy), fill=(255, 255, 255, int(200 * (1 - t) + 30)))
	ginset = int(min(w, h) * 0.12)
	gmask = mask(box=(x0 + ginset, y0 + int(2.5 * S), x1 - ginset, gy1))
	layer.paste(gloss, (0, 0), gmask)

	# Bottom glow
	glow = mask(box=(x0 + int(w * 0.25), y1 - int(h * 0.35), x1 - int(w * 0.25), y1 - int(1.5 * S))).filter(ImageFilter.GaussianBlur(2 * S))
	layer.paste((255, 255, 255, 70), (0, 0), glow)

	base.alpha_composite(layer)


def glossy_button(base, draw, cx, cy, r, rgb, label, label_size=12):
	glossy(base, (cx - r, cy - r, cx + r, cy + r), 'circle', rgb)
	outlined_text(draw, (cx, cy + 0.5), label, font(label_size), (255, 255, 255), outline=mix(rgb, (0, 0, 0), 0.6), width=1, anchor='mm')


def glossy_pill(base, draw, x, cy, w, h, rgb, label, label_size=9):
	glossy(base, (x, cy - h // 2, x + w, cy + h // 2), 'pill', rgb)
	outlined_text(draw, (x + w / 2, cy + 0.5), label, font(label_size), (255, 255, 255), outline=mix(rgb, (0, 0, 0), 0.6), width=1, anchor='mm')


def dpad(base, cx, cy, size):
	arm = size // 3
	grey = (120, 128, 150)
	glossy(base, (cx - arm // 2, cy - size // 2, cx + arm // 2, cy + size // 2), 'pill', grey, radius=3)
	glossy(base, (cx - size // 2, cy - arm // 2, cx + size // 2, cy + arm // 2), 'pill', grey, radius=3)


def circle_pad(base, cx, cy, r):
	glossy(base, (cx - r, cy - r, cx + r, cy + r), 'circle', (70, 76, 95))
	glossy(base, (cx - r + 4, cy - r + 4, cx + r - 4, cy + r - 4), 'circle', (140, 148, 170))


# --- Composition ---

def build():
	img = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 255))
	d = ImageDraw.Draw(img)

	# Background: deep blue gradient with diagonal stripes
	top, bottom = (14, 30, 110), (36, 104, 210)
	for y in range(H * S):
		d.line((0, y, W * S, y), fill=mix(top, bottom, y / (H * S)) + (255,))
	stripes = Image.new('RGBA', img.size, (0, 0, 0, 0))
	sd = ImageDraw.Draw(stripes)
	for x in range(-H * S, W * S, 22 * S):
		sd.polygon([(x, H * S), (x + 10 * S, H * S), (x + 10 * S + H * S, 0), (x + H * S, 0)], fill=(255, 255, 255, 14))
	img.alpha_composite(stripes)

	# Title band
	d.rectangle((0, 0, W * S, 34 * S), fill=(8, 18, 72, 255))
	d.rectangle((0, 34 * S, W * S, 36 * S), fill=(255, 210, 0, 255))
	d.rectangle((0, 36 * S, W * S, 37 * S), fill=(200, 60, 20, 255))
	title = Image.new('RGBA', img.size, (0, 0, 0, 0))
	td = ImageDraw.Draw(title)
	outlined_text(td, (W / 2, 17), 'CONTROLS', font(22), (255, 214, 0), outline=(150, 40, 10), width=2, anchor='mm')
	# Slant the title for a retro arcade look
	title = title.transform(img.size, Image.AFFINE, (1, 0.22, -0.22 * 17 * S, 0, 1, 0), resample=Image.BICUBIC)
	img.alpha_composite(title)

	# Rows: icons on the left, action on the right
	label_x = 180
	rows_y = [56, 88, 120, 150, 178, 206]
	panel = Image.new('RGBA', img.size, (0, 0, 0, 0))
	pd = ImageDraw.Draw(panel)
	for y in rows_y:
		pd.rounded_rectangle((8 * S, (y - 13) * S, (W - 8) * S, (y + 13) * S), radius=7 * S, fill=(0, 10, 50, 90))
	img.alpha_composite(panel)

	big, small = font(15), font(9)

	# Move: Circle Pad + D-Pad
	y = rows_y[0]
	circle_pad(img, 40, y, 11)
	dpad(img, 78, y, 22)
	outlined_text(d, (label_x, y), 'MOVE', big, (255, 255, 255))

	# Jump: A B X Y in the 3DS colors
	y = rows_y[1]
	for i, (lbl, col) in enumerate([('A', (225, 38, 46)), ('B', (245, 190, 25)), ('X', (40, 105, 225)), ('Y', (30, 165, 75))]):
		glossy_button(img, d, 30 + i * 30, y, 11, col, lbl)
	outlined_text(d, (label_x, y), 'JUMP', big, (255, 255, 255))

	# Spin dash hint: Down + any jump button
	y = rows_y[2]
	dpad(img, 34, y, 20)
	d.polygon([(30 * S, (y + 5) * S), (38 * S, (y + 5) * S), (34 * S, (y + 10) * S)], fill=(255, 214, 0, 255))
	outlined_text(d, (56, y), '+', big, (255, 255, 255), anchor='mm')
	glossy_button(img, d, 78, y, 10, (225, 38, 46), 'A', 11)
	outlined_text(d, (label_x, y), 'SPIN DASH', big, (255, 255, 255))

	# Pause / Back
	y = rows_y[3]
	glossy_pill(img, d, 20, y, 56, 16, (90, 98, 120), 'START')
	outlined_text(d, (label_x, y), 'PAUSE', big, (255, 255, 255))
	y = rows_y[4]
	glossy_pill(img, d, 20, y, 56, 16, (90, 98, 120), 'SELECT')
	outlined_text(d, (label_x, y), 'BACK', big, (255, 255, 255))

	# Menus: A confirm, B back
	y = rows_y[5]
	glossy_button(img, d, 30, y, 10, (225, 38, 46), 'A', 11)
	glossy_button(img, d, 92, y, 10, (245, 190, 25), 'B', 11)
	outlined_text(d, (44, y), 'OK', small, (255, 255, 255))
	outlined_text(d, (106, y), 'BACK', small, (255, 255, 255))
	outlined_text(d, (label_x, y), 'MENUS', big, (255, 255, 255))

	# Footer
	outlined_text(d, (W / 2, 230), 'Sonic 3 A.I.R.  -  unofficial 3DS port', small, (200, 220, 255), width=1, anchor='mm')

	return img.resize((W, H), Image.LANCZOS).convert('RGB')


def to_3ds_rgb565(img):
	"""The bottom screen framebuffer is 240x320 and rotated: screen pixel (x, y) is at index x * 240 + (239 - y)."""
	px = img.load()
	out = bytearray(W * H * 2)
	for x in range(W):
		for y in range(H):
			r, g, b = px[x, y]
			value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
			struct.pack_into('<H', out, 2 * (x * H + (H - 1 - y)), value)
	return bytes(out)


if __name__ == '__main__':
	image = build()
	image.save(os.path.join(HERE, 'controls.png'))
	with open(os.path.join(HERE, 'controls.bin'), 'wb') as f:
		f.write(to_3ds_rgb565(image))
	print('Wrote controls.png and controls.bin')
