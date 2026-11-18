#!/bin/sh
# Shared helpers for tools/*.sh.  Sourced, not executed.

say() { printf '\n== %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# Pak and release archives are plain zips.  `zip` is the documented tool; bsdtar
# writes an equivalent archive (same relative paths, unix modes preserved --
# launch.sh must stay executable when the archive is unpacked on the device).
if command -v zip >/dev/null; then
	zipdir() { ( cd "$(dirname "$2")" && zip -qr "$1" "$(basename "$2")" ); }
elif command -v bsdtar >/dev/null; then
	zipdir() { ( cd "$(dirname "$2")" && bsdtar -a -cf "$1" "$(basename "$2")" ); }
else
	die "need zip or bsdtar to build the pak archives"
fi

# sha256 of one file, lowercase hex (the format SHA256SUMS uses).
sha256() { sha256sum "$1" | cut -d' ' -f1; }

# First 8 hex digits of a pin: the identity a core pak records in its `version`
# file and the one the manifest publishes.
short_pin() { printf '%s' "$1" | cut -c1-8; }

# MinarchGL's own tag list, as one line ("LIBRETRO_A LIBRETRO_B ..."), read from
# the per-platform core table.  Comments and blank lines are skipped.
core_tags() {
	sed -n 's/^\(LIBRETRO_[A-Z0-9_]*\)[[:space:]].*/\1/p' "$1" | tr '\n' ' ' | sed 's/ $//'
}
