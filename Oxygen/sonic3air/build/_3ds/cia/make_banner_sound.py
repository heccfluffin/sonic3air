#!/usr/bin/env python3
"""
Generates an original "coin" banner sound (the HOME Menu banner used it before "Ringle", see banner.wav).
Run it with banner.wav as the output to switch back to it.

An original "coin" sound, synthesized from scratch: two metallic clinks (a coin landing and bouncing once),
followed by a short sparkle. A metal coin rings with inharmonic overtones, so each clink is a sum of decaying
sine partials at non-integer frequency ratios, plus a very short noise burst for the impact.

Only uses the Python standard library. Output: 16-bit mono PCM, 22050 Hz (the format bannertool takes).
Usage: python3 make_banner_sound.py [output.wav]     (default: coin.wav)
"""

import math
import random
import struct
import sys
import wave

SAMPLE_RATE = 22050
DURATION = 0.85         # seconds; banner sounds must stay short
PEAK = 0.7              # output peak level (about -3 dBFS)

# Inharmonic overtone ratios and relative levels of a small metal disc
PARTIALS = [(1.00, 1.00), (2.32, 0.55), (4.25, 0.35), (6.63, 0.20)]


def clink(buffer, start, base_frequency, level, decay):
	"""Adds one metallic clink: decaying inharmonic partials and a tiny impact noise."""
	rng = random.Random(int(base_frequency))
	first = int(start * SAMPLE_RATE)
	for ratio, partial_level in PARTIALS:
		frequency = base_frequency * ratio
		if frequency >= SAMPLE_RATE / 2 * 0.9:
			continue
		partial_decay = decay / (1.0 + 0.6 * (ratio - 1.0))		# Higher overtones fade faster
		phase = rng.random() * 2.0 * math.pi
		for i in range(first, len(buffer)):
			t = (i - first) / SAMPLE_RATE
			envelope = math.exp(-t / partial_decay)
			if envelope < 0.001:
				break
			attack = min(1.0, t / 0.002)
			buffer[i] += level * partial_level * attack * envelope * math.sin(2.0 * math.pi * frequency * t + phase)

	# Impact: 4 ms of fading noise
	for i in range(first, min(first + int(0.004 * SAMPLE_RATE), len(buffer))):
		t = (i - first) / SAMPLE_RATE
		buffer[i] += level * 0.25 * (rng.random() * 2.0 - 1.0) * (1.0 - t / 0.004)


def sparkle(buffer, start, frequencies, level):
	"""Adds a few short, soft, high bell tones in quick succession."""
	for index, frequency in enumerate(frequencies):
		first = int((start + index * 0.045) * SAMPLE_RATE)
		for i in range(first, len(buffer)):
			t = (i - first) / SAMPLE_RATE
			envelope = math.exp(-t / 0.06) * min(1.0, t / 0.003)
			if t > 0.01 and envelope < 0.001:
				break
			buffer[i] += level * envelope * math.sin(2.0 * math.pi * frequency * t)


def main():
	output = sys.argv[1] if len(sys.argv) > 1 else "coin.wav"
	buffer = [0.0] * int(DURATION * SAMPLE_RATE)

	clink(buffer, 0.000, 1870.0, 1.00, 0.22)	# Landing
	clink(buffer, 0.105, 2210.0, 0.55, 0.16)	# Bounce: a bit higher and quieter
	sparkle(buffer, 0.20, [3520.0, 4190.0, 4980.0], 0.18)

	# Fade out the end, normalize and write
	fade = int(0.08 * SAMPLE_RATE)
	for i in range(fade):
		buffer[-1 - i] *= i / fade
	peak = max(abs(v) for v in buffer) or 1.0
	samples = [int(max(-1.0, min(1.0, v / peak * PEAK)) * 32767) for v in buffer]

	# The HOME Menu needs the banner sound in stereo (2 channels) and at most 3 seconds long, otherwise it plays
	# beeping / clicking or nothing (see https://www.3dbrew.org/wiki/CBMD)
	stereo = [v for v in samples for _ in range(2)]
	with wave.open(output, "wb") as file:
		file.setnchannels(2)
		file.setsampwidth(2)
		file.setframerate(SAMPLE_RATE)
		file.writeframes(struct.pack("<%dh" % len(stereo), *stereo))
	print(f"Wrote {output}: {DURATION:.2f} s, {SAMPLE_RATE} Hz, 16-bit stereo")


if __name__ == "__main__":
	main()
