#!/bin/sh
# Host-side tests for the parts of the frontend switcher that touch the SD card.
#
# The switcher itself is an aarch64/SDL tool, so it cannot run on the build host;
# the file-level logic is isolated in workspace/all/minarch-switch/switch-core.hpp
# (no SDL) and exercised here against a temporary directory.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/workspace/all/minarch-switch"
OUT="$ROOT/build/host"

command -v g++ >/dev/null || { echo "host g++ is required for make test" >&2; exit 1; }

mkdir -p "$OUT"
g++ -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
	"$SRC/tests/switch-core-test.cpp" -o "$OUT/switch-core-test"
exec "$OUT/switch-core-test"
