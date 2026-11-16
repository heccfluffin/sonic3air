#!/bin/bash
# Packages the 3DS build as an installable .cia (for FBI), with Home Menu icon and banner.
#
# Needs, on your PATH or in TOOLS_DIR:
#   makerom     - https://github.com/3DSGuy/Project_CTR (makerom/)
#   bannertool  - https://github.com/carstene1ns/3ds-bannertool
#
# Usage (from Oxygen/sonic3air/build/_3ds, after ./build.sh):
#   ./build-cia.sh                  -> build/sonic3air.cia
#   TOOLS_DIR=/path/to/tools ./build-cia.sh
set -e
cd "$(dirname "$0")"

find_tool() {
	if [ -n "$TOOLS_DIR" ] && [ -x "$TOOLS_DIR/$1" ]; then echo "$TOOLS_DIR/$1"; return; fi
	command -v "$1" || { echo "Error: '$1' not found. Put it on your PATH or set TOOLS_DIR." >&2; exit 1; }
}
MAKEROM=$(find_tool makerom)
BANNERTOOL=$(find_tool bannertool)

ELF=build/sonic3air.elf
[ -f "$ELF" ] || { echo "Error: $ELF not found, run ./build.sh first." >&2; exit 1; }

"$BANNERTOOL" makebanner -i cia/banner.png -a cia/banner.wav -o build/banner.bnr
"$BANNERTOOL" makesmdh \
	-s "Sonic 3 A.I.R." \
	-l "Angel Island Revisited (unofficial 3DS port)" \
	-p "Eukaryot & contributors, 3DS port" \
	-i icon.png -o build/icon.icn

"$MAKEROM" -f cia -o build/sonic3air.cia -target t -exefslogo \
	-elf "$ELF" -rsf cia/sonic3air.rsf \
	-icon build/icon.icn -banner build/banner.bnr

echo "Built build/sonic3air.cia"
