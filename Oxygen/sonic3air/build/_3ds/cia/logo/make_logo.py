#!/usr/bin/env python3
"""
Builds the CIA's launch logo (shown by the 3DS for a moment when the game starts): makerom's "Homebrew" logo
(base_logo.darc, from makerom's ncch_logo.h, MIT license) with its pictures replaced: "Sonic 3 A.I.R." instead of
"homebrew", the wavy band in the banner's blue, and rings instead of squares. Layout and animation stay the same.

Output: s3air_logo.bcma.lz (LZ11-compressed DARC, padded to 0x2000 bytes like the built-in logos), used by build-cia.sh.
Usage:  python3 make_logo.py      (needs Pillow)
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import etc1  # noqa: E402
import lz11  # noqa: E402

# Pictures inside base_logo.darc: offset of the texture data, texture size (all ETC1)
TEXTURES = {'bubbles': (0x1880, 256, 256), 'logo': (0x9900, 256, 64), 'waves': (0xb980, 256, 64)}
MAX_SIZE = 0x2000


def font(size):
	for name in ('DejaVuSans-Bold.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 'arialbd.ttf'):
		try:
			return ImageFont.truetype(name, size)
		except OSError:
			pass
	raise SystemExit('No bold TrueType font found')


def main():
	darc = bytearray(open(os.path.join(HERE, 'base_logo.darc'), 'rb').read())

	# Logo text, in the 225 x 40 area of "homebrew": bold yellow like the banner's lettering
	logo = Image.new('RGB', (256, 64), (0, 0, 0))
	d = ImageDraw.Draw(logo)
	f = font(29)
	text = 'Sonic 3 A.I.R.'
	box = d.textbbox((0, 0), text, font=f)
	d.text(((225 - (box[2] - box[0])) // 2 - box[0], 20 - (box[3] + box[1]) // 2), text, font=f, fill=(255, 214, 40))

	# Wavy band: the original, recolored to the banner's royal blue
	off, w, h = TEXTURES['waves']
	waves = etc1.decode(bytes(darc[off:off + w * h // 2]), w, h)
	px = waves.load()
	for y in range(h):
		for x in range(w):
			r, g, b = px[x, y]
			px[x, y] = (int(r * 0.25 + g * 0.12), int(g * 0.55), b)

	# Rings instead of the squares, at the squares' places (grey: the animation tints them)
	off, w, h = TEXTURES['bubbles']
	original = etc1.decode(bytes(darc[off:off + w * h // 2]), w, h).convert('L')
	op = original.load()
	seen, rings = set(), []
	for y in range(h):
		for x in range(w):
			if op[x, y] > 30 and (x, y) not in seen:
				stack, points = [(x, y)], []
				while stack:
					p = stack.pop()
					if p in seen or not (0 <= p[0] < w and 0 <= p[1] < h) or op[p] <= 30:
						continue
					seen.add(p)
					points.append(p)
					stack += [(p[0] + 1, p[1]), (p[0] - 1, p[1]), (p[0], p[1] + 1), (p[0], p[1] - 1)]
				if len(points) > 20:
					xs, ys = [p[0] for p in points], [p[1] for p in points]
					rings.append(((min(xs) + max(xs)) // 2, (min(ys) + max(ys)) // 2, (max(xs) - min(xs)) // 2, max(op[p] for p in points)))
	bubbles = Image.new('RGB', (w, h), (0, 0, 0))
	bd = ImageDraw.Draw(bubbles)
	for cx, cy, r, value in rings:
		bd.ellipse((cx - r, cy - r, cx + r, cy + r), outline=(value, value, value), width=max(3, r // 3))

	for name, img in (('logo', logo), ('waves', waves), ('bubbles', bubbles)):
		off, w, h = TEXTURES[name]
		darc[off:off + w * h // 2] = etc1.encode(img, w, h)

	compressed = lz11.compress(bytes(darc))
	if len(compressed) > MAX_SIZE:
		raise SystemExit(f'Logo too big: {len(compressed)} bytes compressed, at most {MAX_SIZE}')
	with open(os.path.join(HERE, 's3air_logo.bcma.lz'), 'wb') as out:
		out.write(compressed + bytes(MAX_SIZE - len(compressed)))
	print(f'Wrote s3air_logo.bcma.lz ({len(compressed)} bytes compressed)')


if __name__ == '__main__':
	main()
