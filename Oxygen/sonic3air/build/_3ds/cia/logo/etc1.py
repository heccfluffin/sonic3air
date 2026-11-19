"""ETC1 decode / encode for 3DS textures (8x8 tiles of 4x4 blocks in Z order, blocks stored little endian)."""
import struct
from PIL import Image

MODIFIERS = [(2, 8), (5, 17), (9, 29), (13, 42), (18, 60), (24, 80), (33, 106), (47, 183)]


def clamp(v):
	return 0 if v < 0 else (255 if v > 255 else v)


def decode_block(block):
	"""block: 64-bit integer in standard ETC1 bit order. Returns 4x4 list of (r, g, b) indexed [y][x]."""
	hi, lo = block >> 32, block & 0xffffffff
	diff = (hi >> 1) & 1
	flip = hi & 1
	t1, t2 = (hi >> 5) & 7, (hi >> 2) & 7
	if diff:
		def ext5(v):
			return (v << 3) | (v >> 2)
		r1, g1, b1 = (hi >> 27) & 31, (hi >> 19) & 31, (hi >> 11) & 31
		dr, dg, db = (hi >> 24) & 7, (hi >> 16) & 7, (hi >> 8) & 7
		dr, dg, db = [v - 8 if v > 3 else v for v in (dr, dg, db)]
		c1 = (ext5(r1), ext5(g1), ext5(b1))
		c2 = (ext5((r1 + dr) & 31), ext5((g1 + dg) & 31), ext5((b1 + db) & 31))
	else:
		c1 = tuple(((hi >> s) & 15) * 17 for s in (28, 20, 12))
		c2 = tuple(((hi >> s) & 15) * 17 for s in (24, 16, 8))
	out = [[None] * 4 for _ in range(4)]
	for x in range(4):
		for y in range(4):
			i = x * 4 + y
			msb, lsb = (lo >> (i + 16)) & 1, (lo >> i) & 1
			second = (y >= 2) if flip else (x >= 2)
			base, table = (c2, t2) if second else (c1, t1)
			small, large = MODIFIERS[table]
			mod = [small, large, -small, -large][lsb | (msb << 1)]
			out[y][x] = tuple(clamp(c + mod) for c in base)
	return out


def tile_positions(width, height):
	"""Yields (x, y) of each 4x4 block in the 3DS storage order."""
	for ty in range(0, height, 8):
		for tx in range(0, width, 8):
			for (bx, by) in ((0, 0), (4, 0), (0, 4), (4, 4)):
				yield tx + bx, ty + by


def decode(data, width, height):
	img = Image.new('RGB', (width, height))
	px = img.load()
	for k, (x0, y0) in enumerate(tile_positions(width, height)):
		block = struct.unpack_from('<Q', data, k * 8)[0]
		pixels = decode_block(block)
		for y in range(4):
			for x in range(4):
				if x0 + x < width and y0 + y < height:
					px[x0 + x, y0 + y] = pixels[y][x]
	return img


def encode_block(pixels):
	"""Simple ETC1 encoder: tries both flip modes in differential mode with averaged sub-block colors and all
	modifier tables, picks the lowest error. pixels: [y][x] of (r, g, b)."""
	best = None
	for flip in (0, 1):
		halves = [[], []]
		for y in range(4):
			for x in range(4):
				second = (y >= 2) if flip else (x >= 2)
				halves[1 if second else 0].append((x, y, pixels[y][x]))
		bases = []
		for half in halves:
			avg = [sum(p[2][c] for p in half) / len(half) for c in range(3)]
			bases.append([clamp(int(round(v * 31 / 255))) if False else min(31, max(0, int(round(v * 31 / 255)))) for v in avg])
		# Differential needs |delta| <= 3/4: clamp the second base towards the first
		b1 = bases[0]
		b2 = [min(b1[c] + 3, max(b1[c] - 4, bases[1][c])) for c in range(3)]
		colors = [tuple((v << 3) | (v >> 2) for v in b1), tuple((v << 3) | (v >> 2) for v in b2)]
		hi = (b1[0] << 27) | (((b2[0] - b1[0]) & 7) << 24) | (b1[1] << 19) | (((b2[1] - b1[1]) & 7) << 16) | (b1[2] << 11) | (((b2[2] - b1[2]) & 7) << 8) | 2 | flip
		lo = 0
		total = 0
		tables = []
		for h, half in enumerate(halves):
			best_t = None
			for t, (small, large) in enumerate(MODIFIERS):
				err = 0
				choices = []
				for (x, y, p) in half:
					options = []
					for idx, mod in enumerate((small, large, -small, -large)):
						c = tuple(clamp(colors[h][i] + mod) for i in range(3))
						options.append((sum((c[i] - p[i]) ** 2 for i in range(3)), idx))
					e, idx = min(options)
					err += e
					choices.append((x, y, idx))
				if best_t is None or err < best_t[0]:
					best_t = (err, t, choices)
			total += best_t[0]
			tables.append(best_t)
		hi |= (tables[0][1] << 5) | (tables[1][1] << 2)
		for (_, _, choices) in tables:
			for (x, y, idx) in choices:
				i = x * 4 + y
				lsb, msb = idx & 1, idx >> 1
				lo |= (lsb << i) | (msb << (i + 16))
		if best is None or total < best[0]:
			best = (total, (hi << 32) | lo)
	return best[1]


def encode(img, width, height):
	img = img.convert('RGB')
	px = img.load()
	out = bytearray()
	for (x0, y0) in tile_positions(width, height):
		pixels = [[px[min(img.size[0] - 1, x0 + x), min(img.size[1] - 1, y0 + y)] for x in range(4)] for y in range(4)]
		out += struct.pack('<Q', encode_block(pixels))
	return bytes(out)
