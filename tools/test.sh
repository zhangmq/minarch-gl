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

# --- the boot-installed pakz ------------------------------------------------
# The two pakz are zips with a root post_install.sh; the boot installer unpacks
# them at the card root and then runs that script (stopping the rest of its
# steps if it fails).  The scripts cannot be exercised by the C++ tests, so this
# replays the boot step against a throwaway card with a stubbed installer.
if [ -f "$ROOT/dist/MinarchGL.Installer.pakz" ] && [ -f "$ROOT/dist/MinarchGL.Full_$PLATFORM.pakz" ]; then
	mkdir -p "$OUT/pakz"
	CARD="$OUT/pakz/card"

	# A pakz is a zip: the device only has unzip.
	for z in "MinarchGL.Installer.pakz" "MinarchGL.Full_$PLATFORM.pakz"; do
		[ "$(head -c 2 "$ROOT/dist/$z")" = "PK" ] || {
			echo "FAIL $z is not a zip" >&2; exit 1; }
	done

	# What the boot installer does: unzip -o -d <card> <pakz>, run the script.
	unpack() { # <pakz> <card>
		rm -rf "$2"
		mkdir -p "$2"
		if command -v unzip >/dev/null; then ( cd "$2" && unzip -oq "$1" ); else ( cd "$2" && bsdtar -xf "$1" ); fi
	}

	# The installer pakz: modes fixed, online-updates default written only when
	# the card has no preference, and a rerun never overrides one.
	unpack "$ROOT/dist/MinarchGL.Installer.pakz" "$CARD"
	ELF="$CARD/Tools/$PLATFORM/MinarchGL Installer.pak/minarch-gl-installer.elf"
	[ -x "$ELF" ] || { echo "FAIL installer pakz elf is not executable" >&2; exit 1; }
	SDCARD_PATH="$CARD" PLATFORM="$PLATFORM" sh "$CARD/post_install.sh" ||
		{ echo "FAIL installer pakz post_install returned non-zero" >&2; exit 1; }
	[ "$(cat "$CARD/.userdata/$PLATFORM/minarch-gl/prefs")" = "online=1" ] ||
		{ echo "FAIL installer pakz default is not online=1" >&2; exit 1; }
	printf 'online=0\n' > "$CARD/.userdata/$PLATFORM/minarch-gl/prefs"
	SDCARD_PATH="$CARD" PLATFORM="$PLATFORM" sh "$CARD/post_install.sh" ||
		{ echo "FAIL installer pakz rerun returned non-zero" >&2; exit 1; }
	[ "$(cat "$CARD/.userdata/$PLATFORM/minarch-gl/prefs")" = "online=0" ] ||
		{ echo "FAIL installer pakz overrode an existing preference" >&2; exit 1; }

	# The full pack: it calls the installer CLI once per component with a file://
	# base URL, defaults online off, keeps an existing preference, removes its
	# staging directory, and still exits 0 when the installer fails.
	unpack "$ROOT/dist/MinarchGL.Full_$PLATFORM.pakz" "$CARD"
	cat > "$ELF" <<'STUB'
#!/bin/sh
echo "$*" >> "${SDCARD_PATH:?}/stub.log"
echo "base=${MINARCHGL_BASE_URL:-none}" >> "${SDCARD_PATH:?}/stub.log"
exit "${STUB_FAIL:-0}"
STUB
	chmod 0755 "$ELF"
	rm -f "$CARD/.userdata/$PLATFORM/minarch-gl/prefs"
	SDCARD_PATH="$CARD" PLATFORM="$PLATFORM" sh "$CARD/post_install.sh" ||
		{ echo "FAIL full pack post_install returned non-zero" >&2; exit 1; }
	grep -q -- '--install' "$CARD/stub.log" || { echo "FAIL full pack did not install the frontend" >&2; exit 1; }
	for t in $(sed -n 's/^\(LIBRETRO_[A-Z0-9_]*\)[[:space:]].*/\1/p' "$ROOT/gles-cores/$PLATFORM/cores.list" | tr '\n' ' ' | sed 's/ $//'); do
		grep -q -- "--core-install $t" "$CARD/stub.log" ||
			{ echo "FAIL full pack did not install $t" >&2; exit 1; }
	done
	grep -q 'base=file://' "$CARD/stub.log" || { echo "FAIL full pack did not use a file:// base URL" >&2; exit 1; }
	[ ! -d "$CARD/MinarchGL.Pack" ] || { echo "FAIL full pack left its staging directory" >&2; exit 1; }
	[ "$(cat "$CARD/.userdata/$PLATFORM/minarch-gl/prefs")" = "online=0" ] ||
		{ echo "FAIL full pack default is not online=0" >&2; exit 1; }
	printf 'online=1\n' > "$CARD/.userdata/$PLATFORM/minarch-gl/prefs"
	SDCARD_PATH="$CARD" PLATFORM="$PLATFORM" sh "$CARD/post_install.sh" ||
		{ echo "FAIL full pack rerun returned non-zero" >&2; exit 1; }
	[ "$(cat "$CARD/.userdata/$PLATFORM/minarch-gl/prefs")" = "online=1" ] ||
		{ echo "FAIL full pack overrode an existing preference" >&2; exit 1; }
	# The first run consumed the payload; unpack again for the failure path.
	unpack "$ROOT/dist/MinarchGL.Full_$PLATFORM.pakz" "$CARD"
	cat > "$ELF" <<'STUB'
#!/bin/sh
echo "$*" >> "${SDCARD_PATH:?}/stub.log"
echo "base=${MINARCHGL_BASE_URL:-none}" >> "${SDCARD_PATH:?}/stub.log"
exit "${STUB_FAIL:-0}"
STUB
	chmod 0755 "$ELF"
	: > "$CARD/stub.log"
	STUB_FAIL=1 SDCARD_PATH="$CARD" PLATFORM="$PLATFORM" sh "$CARD/post_install.sh" ||
		{ echo "FAIL full pack did not survive an installer failure" >&2; exit 1; }
	grep -q 'FAILED' "$CARD/.userdata/$PLATFORM/logs/minarch-gl-full-pack.txt" ||
		{ echo "FAIL full pack failure was not logged" >&2; exit 1; }
	echo "pakz: checks passed"
else
	echo "pakz: no release pakz yet (run make release)"
fi
