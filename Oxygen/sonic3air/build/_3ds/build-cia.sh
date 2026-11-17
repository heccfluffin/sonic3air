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
#
# Optional, set by the CI build:
#   BUILD_NUMBER=52             -> title version shown by FBI (one number that increases with every build, max 65535)
#   BUILD_LABEL=b52-2937545     -> added to the description shown in FBI / HOME Menu
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
LONG_DESCRIPTION="Angel Island Revisited (unofficial 3DS port)"
if [ -n "$BUILD_LABEL" ]; then LONG_DESCRIPTION="$LONG_DESCRIPTION - build $BUILD_LABEL"; fi
VERSION_ARGS=""
if [ -n "$BUILD_NUMBER" ]; then VERSION_ARGS="-ver $BUILD_NUMBER"; fi

"$BANNERTOOL" makesmdh \
	-s "Sonic 3 A.I.R." \
	-l "$LONG_DESCRIPTION" \
	-p "Eukaryot & contributors, 3DS port" \
	-i icon.png -o build/icon.icn

"$MAKEROM" -f cia -o build/sonic3air.cia -target t -exefslogo $VERSION_ARGS \
	-elf "$ELF" -rsf cia/sonic3air.rsf \
	-icon build/icon.icn -banner build/banner.bnr

echo "Built build/sonic3air.cia"
