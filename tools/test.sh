#!/bin/sh
# Host-side tests for the installer.
#
# The installer itself is an aarch64/SDL tool, so it cannot run on the build
# host; the file-level logic is isolated in installer/installer-core.hpp (no
# SDL) and exercised here against temporary directory trees.
#
#   installer-core-test   every state, action, manifest and archive rule
#   manifest-check        the packaged release, read with the same parser
#                         (only when a release has been built in dist/)
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/installer"
OUT="$ROOT/build/host"
PLATFORM=${PLATFORM:-h700}

command -v g++ >/dev/null || { echo "host g++ is required for make test" >&2; exit 1; }

mkdir -p "$OUT"
g++ -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
	"$SRC/tests/installer-core-test.cpp" -o "$OUT/installer-core-test" -lcrypto
"$OUT/installer-core-test"

# Same include path, same code: this is what the release script runs too.
g++ -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
	"$SRC/tests/manifest-check.cpp" -o "$OUT/manifest-check" -lcrypto

if [ -f "$ROOT/dist/core-list.json" ]; then
	"$OUT/manifest-check" "$PLATFORM" "$ROOT/dist/core-list.json" "$ROOT/dist"
else
	echo "manifest-check: no dist/core-list.json yet (run make release)"
fi
