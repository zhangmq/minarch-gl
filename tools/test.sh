#!/bin/sh
# Host-side tests for the parts of the minarch-gl installer that touch the SD card.
#
# The installer itself is an aarch64/SDL tool, so it cannot run on the build host;
# the file-level logic is isolated in installer/installer-core.hpp (no SDL) and
# exercised here against a temporary directory.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/installer"
OUT="$ROOT/build/host"

command -v g++ >/dev/null || { echo "host g++ is required for make test" >&2; exit 1; }

mkdir -p "$OUT"
g++ -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
	"$SRC/tests/installer-core-test.cpp" -o "$OUT/installer-core-test" -lcrypto
exec "$OUT/installer-core-test"
