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

# A core binary has to be the one *this* toolchain makes: aarch64, and nothing
# newer than the image's glibc 2.28 sysroot.  Neither is checked anywhere else,
# and a core built with another compiler links and runs on the builder while
# failing only on the device -- that is how a gcc 12 flycast once asked for
# GLIBC_2.36 on a 2.35 system (see .docs/HANDOVER-20261121-FLYCASTROPHE.md).
# A build rpath is fatal too: it is a build-tree path (DT_RPATH outranks
# LD_LIBRARY_PATH and ld.so.cache, so a stale stub at that path would shadow the
# driver), and no core needs one -- yabasanshiro is the only one that ever did,
# and its recipe now passes -DCMAKE_SKIP_RPATH=ON.
assert_core_elf() { # assert_core_elf <so> <tag>
	_so=$1 _tag=$2
	[ -f "$_so" ] || die "$_tag: no core binary at $_so"
	file "$_so" | grep -q 'ARM aarch64' || die "$_tag: not an aarch64 ELF: $(file "$_so")"
	_bad=$(readelf --version-info "$_so" 2>/dev/null |
		grep -o 'GLIBC_[0-9.]*' | sort -u |
		awk -F'[_.]' '$2 > 2 || ($2 == 2 && $3 > 28) { printf "%s ", $0 }')
	[ -z "$_bad" ] || die "$_tag: needs glibc ${_bad}-- the sysroot this project builds against is 2.28"
	if readelf -d "$_so" | grep -Eq 'RPATH|RUNPATH'; then
		die "$_tag: the core carries a build rpath: $(readelf -d "$_so" | grep -E 'RPATH|RUNPATH' | sed 's/^ *//')"
	fi
}
