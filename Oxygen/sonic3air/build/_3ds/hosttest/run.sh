#!/bin/bash
# Host test for the 3DS port: builds the real 3DS bottom screen, 3DS menu data, profile and file writer code for this PC,
# against a fake 3DS (fake3ds.cpp), runs all scenarios and makes a contact sheet of the bottom screen pictures.
# Needs g++, zlib (zlib1g-dev) and Python 3 with Pillow. Output goes to ./out (not committed).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../../../oxygenengine/source"
ART="$HERE/../bottomscreen"
OUT="$HERE/out"
rm -rf "$OUT"
mkdir -p "$OUT/gen" "$OUT/shots"

# The embedded artwork, like devkitPro's bin2s does for the 3DS build
python3 - "$ART" "$OUT/gen" <<'PY'
import os, sys
art, gen = sys.argv[1], sys.argv[2]
files = [f for f in os.listdir(art) if f.startswith('s3air_bottom_screen_') and f.endswith('.bin')] + ['s3air_theme_template.png']
with open(os.path.join(gen, 'embedded.c'), 'w') as c:
	c.write('#include <stdint.h>\n')
	for f in sorted(files):
		name = f.replace('.', '_')
		data = open(os.path.join(art, f), 'rb').read()
		with open(os.path.join(gen, name + '.h'), 'w') as h:
			h.write(f'#pragma once\n#include <stdint.h>\n#ifdef __cplusplus\nextern "C" {{\n#endif\nextern const uint8_t {name}[];\nextern const uint32_t {name}_size;\n#ifdef __cplusplus\n}}\n#endif\n')
		c.write(f'const uint8_t {name}[] = {{' + ','.join(str(b) for b in data) + '};\n')
		c.write(f'const uint32_t {name}_size = {len(data)};\n')
PY

FLAGS="-std=c++17 -O1 -g -Wall -Wno-unused-function -DPLATFORM_3DS -DN3DS_PROFILER -I$HERE/fake -I$HERE -I$SRC -I$OUT/gen"
gcc -c -O1 "$OUT/gen/embedded.c" -o "$OUT/embedded.o"
g++ $FLAGS -include "$HERE/fake/3ds.h" \
	"$SRC/oxygen/platform/3ds/N3DSBottomScreen.cpp" "$SRC/oxygen/platform/3ds/N3DSShop.cpp" "$SRC/oxygen/platform/3ds/N3DSProfile.cpp" \
	"$SRC/oxygen/platform/3ds/N3DSFileWriter.cpp" "$SRC/oxygen/platform/3ds/N3DSJournal.cpp" "$SRC/oxygen/platform/3ds/N3DSChallenges.cpp" "$HERE/fake3ds.cpp" "$HERE/main.cpp" "$OUT/embedded.o" \
	-Wl,--wrap=time -Wl,--wrap=write -lz -pthread -o "$OUT/hosttest"

FAILED=0
for scenario in clock24 clock12 idle timeofday nightmode taps greeting birthday unusualname filewriter shopreload postcards postcardsall rewards rewardsreload challenges challengeslots challengetypes challengetypes2; do
	echo "== $scenario"
	mkdir -p "$OUT/sd_$scenario/sdmc:"		# The SD card itself, which always exists on the 3DS
	(cd "$OUT/sd_$scenario" && "$OUT/hosttest" "$scenario" "$OUT/shots") || FAILED=1
done

python3 - "$OUT/shots" "$OUT/bottom_screen_tests.png" <<'PY'
import os, sys
from PIL import Image, ImageDraw
shots = sorted(f for f in os.listdir(sys.argv[1]) if f.endswith('.ppm'))
cols = 3
sheet = Image.new('RGB', (cols * 330 + 10, ((len(shots) + cols - 1) // cols) * 262 + 10), (30, 30, 30))
d = ImageDraw.Draw(sheet)
for i, f in enumerate(shots):
	x, y = 10 + (i % cols) * 330, 10 + (i // cols) * 262
	img = Image.open(os.path.join(sys.argv[1], f))
	img.save(os.path.join(sys.argv[1], f[:-4] + '.png'))
	d.text((x, y), f[:-4], fill=(255, 255, 255))
	sheet.paste(img, (x, y + 14))
sheet.save(sys.argv[2])
print(f'Contact sheet with {len(shots)} pictures: {sys.argv[2]}')
PY

if [ $FAILED -ne 0 ]; then echo "SOME CHECKS FAILED"; exit 1; fi
echo "All checks passed"
