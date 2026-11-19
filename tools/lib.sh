#!/bin/sh
# Shared helpers for tools/*.sh.  Sourced, not executed.

say() { printf '\n== %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# Pak and release archives are plain zips.  `zip` is the documented tool; bsdtar
# writes an equivalent archive (same relative paths, unix modes preserved --
# launch.sh must stay executable when the archive is unpacked on the device).
# The format is forced for bsdtar: its -a is driven by the extension, and a
# .pakz extension is not one it knows -- left alone it writes a tar named .pakz,
# which the device's unzip cannot read (it happened; this is the fix).
if command -v zip >/dev/null; then
	zipdir() { ( cd "$(dirname "$2")" && zip -qr "$1" "$(basename "$2")" ); }
	zipcontents() { _out=$1; _dir=$2; shift 2; ( cd "$_dir" && zip -qr "$_out" "$@" ); }
elif command -v bsdtar >/dev/null; then
	zipdir() { ( cd "$(dirname "$2")" && bsdtar --format=zip -cf "$1" "$(basename "$2")" ); }
	zipcontents() { _out=$1; _dir=$2; shift 2; ( cd "$_dir" && bsdtar --format=zip -cf "$_out" "$@" ); }
else
	die "need zip or bsdtar to build the pak archives"
fi

# List an archive's entries, for the checks after building one.
ziplist() { bsdtar -tf "$1" 2>/dev/null || unzip -Z1 "$1" 2>/dev/null; }

# sha256 of one file, lowercase hex (the format SHA256SUMS uses).
sha256() { sha256sum "$1" | cut -d' ' -f1; }

# First 8 hex digits of a pin: the identity a core pak records in its `version`
# file and the one the manifest publishes.
short_pin() { printf '%s' "$1" | cut -c1-8; }

# What a core pak and the manifest publish as the core's identity.  A core's
# binary can change without its source commit changing when this repository
# patches it (patches/<core>.patch), and the installer compares this string:
# an unchanged pin would leave every installed copy "current".  So a patched
# core gets a four-hex suffix derived from the patch; unpatched cores keep the
# commit prefix alone.
core_pin() { # core_pin <core> <commit> <patches-dir>
	_pin=$(short_pin "$2")
	if [ -f "$3/$1.patch" ]; then
		_pin="$_pin.$(sha256sum "$3/$1.patch" | cut -c1-4)"
	fi
	printf '%s' "$_pin"
}

# MinarchGL's own tag list, as one line ("LIBRETRO_A LIBRETRO_B ..."), read from
# the per-platform core table.  Comments and blank lines are skipped.
core_tags() {
	sed -n 's/^\(LIBRETRO_[A-Z0-9_]*\)[[:space:]].*/\1/p' "$1" | tr '\n' ' ' | sed 's/ $//'
}
