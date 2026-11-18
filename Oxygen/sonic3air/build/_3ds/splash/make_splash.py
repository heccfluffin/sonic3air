#!/usr/bin/env python3
"""
Generates the 3DS loading screen (shown on the top screen from the moment the game opens until the first frame).

Output:
  s3air_splash.bin   400 x 240 pixels, RGBA (bytes R, G, B, A per pixel, rows top to bottom), zlib-compressed.
                     Embedded into the game by CMakeLists.txt; the game decompresses it at startup.
  splash_preview.png What it looks like.

Usage:
  python3 make_splash.py                   -> the default design (text and simple shapes, no game artwork)
  python3 make_splash.py --image my.png    -> use your own 400 x 240 picture instead

Needs Pillow (pip install pillow).
"""

import argparse
import os
import zlib

from PIL import Image, ImageDraw, ImageFont

WIDTH, HEIGHT = 400, 240
HERE = os.path.dirname(os.path.abspath(__file__))


def load_font(size, bold=True):
	candidates = ["DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf",
				  "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
				  "arialbd.ttf" if bold else "arial.ttf"]
	for name in candidates:
		try:
			return ImageFont.truetype(name, size)
		except OSError:
			pass
	return ImageFont.load_default()


def centered_text(draw, y, text, font, fill, shadow=None):
	box = draw.textbbox((0, 0), text, font=font)
	x = (WIDTH - (box[2] - box[0])) // 2 - box[0]
	if shadow:
		draw.text((x + 2, y + 2), text, font=font, fill=shadow)
	draw.text((x, y), text, font=font, fill=fill)


def default_design():
	img = Image.new("RGB", (WIDTH, HEIGHT))
	draw = ImageDraw.Draw(img)

	# Sky: dark navy at the top to blue at the horizon
	horizon = 168
	for y in range(horizon):
		t = y / horizon
		draw.line([(0, y), (WIDTH, y)], fill=(int(10 + 30 * t), int(20 + 70 * t), int(60 + 130 * t)))
	# Sea: deeper blue with a few light wave lines
	for y in range(horizon, HEIGHT):
		t = (y - horizon) / (HEIGHT - horizon)
		draw.line([(0, y), (WIDTH, y)], fill=(int(16 - 8 * t), int(70 - 30 * t), int(150 - 50 * t)))
	for i, y in enumerate(range(horizon + 8, HEIGHT, 14)):
		offset = (i * 37) % 60
		for x in range(-60 + offset, WIDTH, 90):
			draw.line([(x, y), (x + 36, y)], fill=(90, 150, 220), width=2)
	draw.line([(0, horizon), (WIDTH, horizon)], fill=(150, 200, 255), width=1)

	title_font = load_font(40)
	sub_font = load_font(18)
	small_font = load_font(11, bold=False)
	centered_text(draw, 52, "Sonic 3 A.I.R.", title_font, fill=(255, 214, 40), shadow=(20, 30, 80))
	centered_text(draw, 102, "for New 3DS", sub_font, fill=(235, 240, 255), shadow=(20, 30, 80))
	centered_text(draw, 138, "Loading...", sub_font, fill=(255, 255, 255), shadow=(20, 30, 80))
	centered_text(draw, 206, "Unofficial port by heccfluffin", small_font, fill=(220, 230, 255))
	centered_text(draw, 220, "Original game by Eukaryot & contributors", small_font, fill=(190, 205, 240))
	return img


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--image", help="Your own 400 x 240 picture to use instead of the default design")
	args = parser.parse_args()

	if args.image:
		img = Image.open(args.image).convert("RGB")
		if img.size != (WIDTH, HEIGHT):
			raise SystemExit(f"The picture must be exactly {WIDTH} x {HEIGHT} pixels (it is {img.size[0]} x {img.size[1]})")
	else:
		img = default_design()

	img.save(os.path.join(HERE, "splash_preview.png"))
	raw = img.convert("RGBA").tobytes()		# R, G, B, A per pixel, rows top to bottom
	with open(os.path.join(HERE, "s3air_splash.bin"), "wb") as file:
		file.write(zlib.compress(raw, 9))
	print(f"Wrote s3air_splash.bin ({len(raw)} bytes uncompressed) and splash_preview.png")


if __name__ == "__main__":
	main()
