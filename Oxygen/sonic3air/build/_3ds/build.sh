#!/bin/bash
# Builds sonic3air.3dsx using the official devkitPro Docker image.
# Usage: ./build.sh            (from Oxygen/sonic3air/build/_3ds)
set -e
REPO_ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
docker run --rm -v "$REPO_ROOT":/src -w /src/Oxygen/sonic3air/build/_3ds devkitpro/devkitarm:latest bash -c '
	set -e
	/opt/devkitpro/portlibs/3ds/bin/arm-none-eabi-cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j"$(nproc)" '"$*"'
'
