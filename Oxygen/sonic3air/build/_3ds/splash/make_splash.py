#!/usr/bin/env python3
"""
Generates the 3DS loading screen (shown on the top screen from the moment the game opens until the first frame).

Output:
  s3air_splash.bin   400 x 240 pixels, RGBA (bytes R, G, B, A per pixel, rows top to bottom), zlib-compressed.
                     Embedded into the game by CMakeLists.txt; the game decompresses it at startup.
  splash_preview.png What it looks like.

Usage:
  python3 make_splash.py                   -> the default design (Frutiger Aero style, drawn from scratch, no game artwork)
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
	"""Frutiger Aero style: glossy sky, light rays, bubbles, a green hill and a glass panel - all drawn from scratch."""
	from PIL import ImageFilter
	import math
	import random

	rng = random.Random(3)
	img = Image.new("RGBA", (WIDTH, HEIGHT))
	px = img.load()

	# Sky: deep blue at the top to bright cyan near the horizon
	for y in range(HEIGHT):
		t = y / (HEIGHT - 1)
		r = int(20 + 120 * t)
		g = int(90 + 140 * t)
		b = int(200 + 50 * t)
		for x in range(WIDTH):
			px[x, y] = (r, g, b, 255)

	def overlay(layer, blur=0):
		nonlocal img
		if blur:
			layer = layer.filter(ImageFilter.GaussianBlur(blur))
		img = Image.alpha_composite(img, layer)

	# Soft light rays from the top left
	rays = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
	d = ImageDraw.Draw(rays)
	origin = (-40, -60)
	for i in range(7):
		angle = math.radians(18 + i * 9)
		spread = math.radians(2.2)
		far = 700
		p1 = (origin[0] + far * math.cos(angle - spread), origin[1] + far * math.sin(angle - spread))
		p2 = (origin[0] + far * math.cos(angle + spread), origin[1] + far * math.sin(angle + spread))
		d.polygon([origin, p1, p2], fill=(255, 255, 255, 38))
	overlay(rays, blur=6)

	# Sun glow
	glow = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
	d = ImageDraw.Draw(glow)
	d.ellipse([-70, -90, 90, 70], fill=(255, 255, 240, 150))
	overlay(glow, blur=30)

	# Flowing aurora ribbons
	ribbons = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
	d = ImageDraw.Draw(ribbons)
	for k, (base, amp, alpha) in enumerate([(120, 18, 70), (135, 14, 50), (150, 22, 40)]):
		points = [(x, base + amp * math.sin(x / 55.0 + k * 1.3)) for x in range(-10, WIDTH + 11, 5)]
		d.line(points, fill=(255, 255, 255, alpha), width=3)
	overlay(ribbons, blur=1.5)

	# Green glossy hill at the bottom
	hill = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
	d = ImageDraw.Draw(hill)
	d.ellipse([-120, 188, 520, 420], fill=(70, 185, 40, 255))
	d.ellipse([-60, 196, 300, 330], fill=(110, 215, 60, 255))
	overlay(hill)
	shine = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
	d = ImageDraw.Draw(shine)
	d.ellipse([20, 198, 240, 222], fill=(220, 255, 180, 120))
	overlay(shine, blur=4)

	# Bubbles: translucent, with a rim and a highlight
	bubbles = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
	d = ImageDraw.Draw(bubbles)
	for _ in range(16):
		r = rng.randint(6, 22)
		cx = rng.randint(10, WIDTH - 10)
		cy = rng.randint(10, 190)
		if 40 < cx < 360 and 30 < cy < 175:		# Keep the panel area clear
			continue
		d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255, 30), outline=(255, 255, 255, 140), width=1)
		h = max(2, r // 3)
		d.ellipse([cx - r // 2 - h // 2, cy - r // 2 - h // 2, cx - r // 2 + h, cy - r // 2 + h], fill=(255, 255, 255, 200))
	overlay(bubbles, blur=0.6)

	# Glass panel: rounded, translucent, with a gloss highlight on its upper half
	panel_box = [50, 38, 350, 168]
	shadow = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
	ImageDraw.Draw(shadow).rounded_rectangle([panel_box[0] + 3, panel_box[1] + 5, panel_box[2] + 3, panel_box[3] + 5], radius=20, fill=(0, 40, 90, 90))
	overlay(shadow, blur=5)
	panel = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
	d = ImageDraw.Draw(panel)
	d.rounded_rectangle(panel_box, radius=20, fill=(255, 255, 255, 70), outline=(255, 255, 255, 190), width=2)
	overlay(panel)
	gloss = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
	d = ImageDraw.Draw(gloss)
	d.rounded_rectangle([panel_box[0] + 6, panel_box[1] + 4, panel_box[2] - 6, panel_box[1] + 52], radius=16, fill=(255, 255, 255, 70))
	overlay(gloss, blur=1)

	# Text
	d = ImageDraw.Draw(img)
	title_font = load_font(34)
	sub_font = load_font(16)
	small_font = load_font(11, bold=False)
	centered_text(d, 58, "Sonic 3 A.I.R.", title_font, fill=(255, 255, 255), shadow=(20, 70, 140))
	centered_text(d, 102, "for New 3DS", sub_font, fill=(235, 248, 255), shadow=(20, 70, 140))

	# Glossy "Loading..." pill
	pill_box = [150, 128, 250, 152]
	pill = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
	d2 = ImageDraw.Draw(pill)
	d2.rounded_rectangle(pill_box, radius=12, fill=(40, 170, 60, 255), outline=(220, 255, 210, 255), width=1)
	d2.rounded_rectangle([pill_box[0] + 3, pill_box[1] + 2, pill_box[2] - 3, pill_box[1] + 11], radius=8, fill=(255, 255, 255, 110))
	overlay(pill)
	d = ImageDraw.Draw(img)
	pill_font = load_font(13)
	centered_text(d, 133, "Loading...", pill_font, fill=(255, 255, 255), shadow=(20, 90, 30))

	centered_text(d, 212, "Unofficial port by heccfluffin", small_font, fill=(255, 255, 255), shadow=(30, 90, 20))
	centered_text(d, 225, "Original game by Eukaryot & contributors", small_font, fill=(235, 255, 225), shadow=(30, 90, 20))
	return img.convert("RGB")


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
