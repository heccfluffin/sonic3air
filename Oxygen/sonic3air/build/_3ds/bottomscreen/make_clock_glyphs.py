#!/usr/bin/env python3
"""
Generates the small pictures for the clock and battery in the title bar of the 3DS bottom screen (Sonic 3 A.I.R. 3DS port):
digits, a colon, "AM" / "PM", a battery in 6 charge levels and a lightning bolt shown next to it while charging. They're white
with a dark outline, so they work on every theme. Drawn from scratch here, like the rest of the bottom screen.

Output, next to this script:
  s3air_bottom_screen_clock.bin   Embedded into the game by CMakeLists.txt (format below)
  clock_glyphs_preview.png        All glyphs, enlarged

Format (zlib-compressed as a whole), numbers little endian:
  4 bytes  "S3BC"
  u16      version (1)
  u16      number of glyphs
  per glyph: u16 width, u16 height, u32 offset of its pixels (from the start of the uncompressed data)
  pixels:  RGBA (straight alpha), rows top to bottom
The order of the glyphs must match "Glyph" in N3DSBottomScreen.cpp.

Usage:  python3 make_clock_glyphs.py      (needs Pillow: pip install pillow)
"""

import os
import struct
import zlib

from PIL import Image, ImageDraw, ImageFont

S = 4                       # Supersampling factor
HERE = os.path.dirname(os.path.abspath(__file__))
FONT_CANDIDATES = [
	'/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
	'DejaVuSans-Bold.ttf',
	'arialbd.ttf',
]

HEIGHT = 18                 # All glyphs have the same height, so they line up
WHITE = (255, 255, 255, 255)
OUTLINE = (0, 25, 60, 220)
SHADOW = (0, 30, 70, 110)


def font(size):
	for path in FONT_CANDIDATES:
		try:
			return ImageFont.truetype(path, int(size * S))
		except OSError:
			pass
	raise SystemExit('No bold TrueType font found, edit FONT_CANDIDATES')


def text_glyph(string, width, size):
	img = Image.new('RGBA', (width * S, HEIGHT * S), (0, 0, 0, 0))
	d = ImageDraw.Draw(img)
	fnt = font(size)
	x, y = width * S / 2, HEIGHT * S / 2
	d.text((x + 0.6 * S, y + 1.2 * S), string, font=fnt, fill=SHADOW, anchor='mm', stroke_width=S, stroke_fill=SHADOW)
	d.text((x, y), string, font=fnt, fill=WHITE, anchor='mm', stroke_width=S, stroke_fill=OUTLINE)
	return img.resize((width, HEIGHT), Image.LANCZOS)


BATTERY_WIDTH = 24


def battery_glyph(level):
	"""Battery with 'level' of 5 bars filled (0 = empty)"""
	img = Image.new('RGBA', (BATTERY_WIDTH * S, HEIGHT * S), (0, 0, 0, 0))
	d = ImageDraw.Draw(img)
	x0, y0, x1, y1 = 2, 4, 19, 14		# Body, in pixels
	tip = (19, 7, 22, 11)
	box = lambda b, grow=0.0: [v * S for v in (b[0] - grow, b[1] - grow, b[2] + grow, b[3] + grow)]

	# Shadow and dark outline, then the white frame
	d.rounded_rectangle([v + (0.6 if i % 2 == 0 else 1.2) * S for i, v in enumerate(box((x0, y0, x1, y1), 1.0))], radius=3 * S, fill=SHADOW)
	d.rounded_rectangle(box((x0, y0, x1, y1), 1.0), radius=3 * S, fill=OUTLINE)
	d.rounded_rectangle(box(tip, 1.0), radius=S, fill=OUTLINE)
	d.rounded_rectangle(box((x0, y0, x1, y1)), radius=2 * S, fill=WHITE)
	d.rounded_rectangle(box(tip), radius=S // 2, fill=WHITE)
	d.rounded_rectangle(box((x0 + 1.5, y0 + 1.5, x1 - 1.5, y1 - 1.5)), radius=S, fill=(20, 35, 60, 255))

	# Charge: green, yellow when low, red when nearly empty
	color = (70, 215, 90, 255) if level >= 3 else (250, 205, 40, 255) if level == 2 else (240, 60, 50, 255)
	inner_x0, inner_x1 = x0 + 2.5, x1 - 2.5
	bar_w = (inner_x1 - inner_x0) / 5
	for i in range(max(level, 1 if level == 0 else 0)):
		bx0 = inner_x0 + i * bar_w
		fill = color if level > 0 else (240, 60, 50, 140)
		d.rectangle(box((bx0 + 0.25, y0 + 2.5, bx0 + bar_w - 0.25, y1 - 2.5)), fill=fill)
	return img.resize((BATTERY_WIDTH, HEIGHT), Image.LANCZOS)


BOLT_WIDTH = 11


def bolt_glyph():
	"""Lightning bolt, shown left of the battery while charging"""
	img = Image.new('RGBA', (BOLT_WIDTH * S, HEIGHT * S), (0, 0, 0, 0))
	d = ImageDraw.Draw(img)
	points = [(6.0, 1.0), (0.8, 10.2), (4.6, 10.2), (3.0, 17.0), (10.2, 6.8), (6.4, 6.8), (9.6, 1.0)]
	shadow = [((x + 0.6) * S, (y + 1.2) * S) for x, y in points]
	d.polygon(shadow, fill=SHADOW, outline=SHADOW, width=int(1.6 * S))
	d.polygon([(x * S, y * S) for x, y in points], fill=(255, 225, 40, 255), outline=OUTLINE, width=int(0.9 * S))
	return img.resize((BOLT_WIDTH, HEIGHT), Image.LANCZOS)


def main():
	# Order must match "Glyph" in N3DSBottomScreen.cpp
	glyphs = [text_glyph(str(digit), 10, 11) for digit in range(10)]
	glyphs.append(text_glyph(':', 6, 11))
	glyphs.append(text_glyph('AM', 19, 8))
	glyphs.append(text_glyph('PM', 19, 8))
	glyphs += [battery_glyph(level) for level in range(6)]
	glyphs.append(bolt_glyph())

	header = bytearray(b'S3BC') + struct.pack('<HH', 1, len(glyphs))
	data_offset = len(header) + 8 * len(glyphs)
	table = bytearray()
	pixels = bytearray()
	for img in glyphs:
		table += struct.pack('<HHI', img.width, img.height, data_offset + len(pixels))
		pixels += img.tobytes()
	with open(os.path.join(HERE, 's3air_bottom_screen_clock.bin'), 'wb') as f:
		f.write(zlib.compress(bytes(header + table + pixels), 9))

	# Preview: on a few theme-like backgrounds, enlarged
	sample = Image.new("RGBA", (360, 3 * 26), (0, 0, 0, 255))
	for row, bg in enumerate([(34, 104, 214), (255, 180, 90), (236, 236, 252)]):
		strip = Image.new("RGBA", (360, 26), bg + (255,))
		x = 4
		for img in glyphs:
			strip.alpha_composite(img, (x, 4))
			x += img.width + 2
		sample.alpha_composite(strip, (0, row * 26))
	sample.resize((sample.width * 3, sample.height * 3), Image.NEAREST).convert('RGB').save(os.path.join(HERE, 'clock_glyphs_preview.png'))
	print('Wrote s3air_bottom_screen_clock.bin and clock_glyphs_preview.png')


if __name__ == '__main__':
	main()
