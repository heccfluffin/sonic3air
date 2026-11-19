def compress(data):
	"""LZ11 compression (greedy, hash-chain match search)."""
	out = bytearray([0x11, len(data) & 0xff, (len(data) >> 8) & 0xff, (len(data) >> 16) & 0xff])
	pos = 0
	n = len(data)
	table = {}
	while pos < n:
		flag_pos = len(out)
		out.append(0)
		flags = 0
		for bit in range(8):
			if pos >= n:
				break
			best_len, best_disp = 0, 0
			if pos + 3 <= n:
				key = data[pos:pos + 3]
				for cand in reversed(table.get(key, [])[-64:]):
					disp = pos - cand
					if disp > 0x1000:
						continue
					length = 0
					maxlen = min(0x10110, n - pos)
					while length < maxlen and data[cand + length] == data[pos + length]:
						length += 1
					if length > best_len:
						best_len, best_disp = length, disp
						if length >= 0x110:
							break
			if best_len >= 3:
				flags |= 0x80 >> bit
				d = best_disp - 1
				if best_len <= 0x10:
					out += bytes([((best_len - 1) << 4) | (d >> 8), d & 0xff])
				elif best_len <= 0x110:
					l = best_len - 0x11
					out += bytes([l >> 4, ((l & 0xf) << 4) | (d >> 8), d & 0xff])
				else:
					l = best_len - 0x111
					out += bytes([0x10 | (l >> 12), (l >> 4) & 0xff, ((l & 0xf) << 4) | (d >> 8), d & 0xff])
				for k in range(best_len):
					if pos + k + 3 <= n:
						table.setdefault(data[pos + k:pos + k + 3], []).append(pos + k)
				pos += best_len
			else:
				if pos + 3 <= n:
					table.setdefault(data[pos:pos + 3], []).append(pos)
				out.append(data[pos])
				pos += 1
		out[flag_pos] = flags
	while len(out) % 4:
		out.append(0)
	return bytes(out)
