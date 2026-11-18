#!/bin/sh
# Turn the staged build into the assets of one release.
#
#   make release
#
# Produces, in dist/:
#
#   minarch-gl_<plat>.elf                the frontend, one per platform
#   LIBRETRO_<TAG>_<plat>.pak.tar.gz     one core pak per tag
#   MinarchGL.Installer.pak.zip          the installer (lite: no payload)
#   MinarchGL.Uninstall.pakz             full removal, run by the boot installer
#   core-list.json                       the manifest the installer reads
#   SHA256SUMS                           every asset above, sha256 + name
#
# Nothing here is hand-written: the pins come from gles-cores/<plat>/makefile,
# the tags and ROM folders from gles-cores/<plat>/cores.list, and every checksum
# from the file itself.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/lib.sh"

PLAT=${PLATFORM:-h700}
DIST="$ROOT/dist"
PAKS="$ROOT/build/paks"
CORES_MK="$ROOT/gles-cores/$PLAT/makefile"
CORES_LIST="$ROOT/gles-cores/$PLAT/cores.list"

pin() { sed -n "s/^$1_$2[[:space:]]*=[[:space:]]*//p" "$CORES_MK" | head -1; }

# A value interpolated into JSON must not be able to break out of its string.
need_json_safe() {
	case "$2" in
	*'"'*|*'\'*) die "$1 contains a quote or backslash: $2" ;;
	esac
	[ -n "$2" ] || die "$1 is empty"
}

[ -f "$DIST/minarch-gl.elf" ] || die "no dist/minarch-gl.elf: run tools/build.sh first"
[ -f "$DIST/minarch-gl-installer.elf" ] || die "no dist/minarch-gl-installer.elf: run BUILD_CORES=1 tools/build.sh"
[ -d "$PAKS/MinarchGL Installer.pak" ] || die "no staged installer pak: run BUILD_CORES=1 tools/build.sh"
[ -f "$CORES_LIST" ] || die "missing $CORES_LIST"

say "release assets ($PLAT)"

# --- frontend ---------------------------------------------------------------
# One binary per platform; the platform name is the only thing in the file name,
# because the frontend has no version string of its own (its identity is the
# sha256 in SHA256SUMS below).
cp -f "$DIST/minarch-gl.elf" "$DIST/minarch-gl_$PLAT.elf"
echo "frontend: dist/minarch-gl_$PLAT.elf" >&2

# --- core paks --------------------------------------------------------------
# Each archive holds the pak's files at its top level, so the installer can
# extract straight into Emus/<plat>/.tmp-<TAG> and rename that to <TAG>.pak.
entries=""
missing=""
while read -r tag core folders; do
	case "$tag" in LIBRETRO_*) ;; *) continue ;; esac
	need_json_safe "tag" "$tag"
	need_json_safe "core" "$core"
	need_json_safe "folders" "$folders"
	pin_value=$(short_pin "$(pin "$core" HASH)")
	[ -n "$pin_value" ] || die "no ${core}_HASH in $CORES_MK"
	pak="$PAKS/$tag.pak"
	if [ ! -d "$pak" ]; then
		missing="$missing $tag"
		continue
	fi
	[ -f "$pak/${core}_libretro.so" ] || die "$pak has no ${core}_libretro.so"
	[ -f "$pak/version" ] || die "$pak has no version file"
	asset="${tag}_${PLAT}.pak.tar.gz"
	rm -f "$DIST/$asset"
	tar -czf "$DIST/$asset" -C "$pak" .
	# The files the installer needs to find at the archive's top level.
	tar -tzf "$DIST/$asset" | grep -qx './launch.sh' ||
		die "$asset does not hold a pak (no ./launch.sh)"
	tar -tzf "$DIST/$asset" | grep -qx "./${core}_libretro.so" ||
		die "$asset does not hold ${core}_libretro.so"
	size=$(wc -c < "$DIST/$asset" | tr -d ' ')
	sum=$(sha256 "$DIST/$asset")
	# folders: "Sega Dreamcast;Sega Naomi" -> ["Sega Dreamcast", "Sega Naomi"].
	# Split on ';' only: the names contain spaces.
	json_folders=""
	old_ifs=$IFS
	IFS=';'
	for f in $(printf '%s' "$folders" | sed 's/[[:space:]]*$//'); do
		IFS=$old_ifs
		need_json_safe "folder" "$f"
		[ -z "$json_folders" ] || json_folders="$json_folders, "
		json_folders="$json_folders\"$f\""
		IFS=';'
	done
	IFS=$old_ifs
	entry="		{\"tag\": \"$tag\", \"name\": \"$core\", \"pin\": \"$pin_value\", \"file\": \"$asset\", \"sha256\": \"$sum\", \"size\": $size, \"folders\": [$json_folders]}"
	[ -z "$entries" ] || entries="$entries,
"
	entries="$entries$entry"
	echo "core: dist/$asset ($size B, pin $pin_value)" >&2
done < "$CORES_LIST"

if [ -n "$missing" ]; then
	if [ "${ALLOW_PARTIAL:-0}" = "1" ]; then
		echo "warning: publishing a partial core list, missing:$missing" >&2
	else
		die "no staged pak for:$missing (build those cores, or set ALLOW_PARTIAL=1)"
	fi
fi
[ -n "$entries" ] || die "no core paks to publish"

# --- manifest ---------------------------------------------------------------
# Only cores: the frontend is identified by its sha256 in SHA256SUMS, and the
# cores carry their own checksum here because the installer gets their file name
# from this table.  No version numbers anywhere -- a core's identity is its pin.
cat > "$DIST/core-list.json" <<EOF
{
	"schema": 1,
	"platforms": {
		"$PLAT": {
			"cores": [
$entries
			]
		}
	}
}
EOF
echo "manifest: dist/core-list.json" >&2

# Parse the manifest with the same code the installer runs (if it has been built
# on the host), so the generator and the parser cannot drift apart.  It also
# re-checks every asset against its published checksum and size.
if [ -x "$ROOT/build/host/manifest-check" ]; then
	"$ROOT/build/host/manifest-check" "$PLAT" "$DIST/core-list.json" "$DIST" ||
		die "the generated manifest does not match the assets"
else
	echo "note: build/host/manifest-check missing (make test builds it); manifest not re-checked" >&2
fi

# --- installer pak and the uninstall pakz -----------------------------------
zipdir "$DIST/MinarchGL.Installer.pak.zip" "$PAKS/MinarchGL Installer.pak"
echo "pak: dist/MinarchGL.Installer.pak.zip" >&2

# The uninstall script needs the tag list, and the only slice of this project
# that knows it is cores.list.  It is baked in at release time: the script runs
# at boot, before any installer UI exists, so it cannot read a manifest.
tags=$(core_tags "$CORES_LIST")
uninstall_src="$ROOT/build/uninstall"
rm -rf "$uninstall_src"
mkdir -p "$uninstall_src"
sed "s|^TAGS=.*|TAGS=\"$tags\"|" "$ROOT/paks/uninstall/post_install.sh.in" > "$uninstall_src/post_install.sh"
[ "$(sed -n 's/^TAGS="\(.*\)"$/\1/p' "$uninstall_src/post_install.sh")" = "$tags" ] ||
	die "cannot write the tag list into the uninstall script"
sh -n "$uninstall_src/post_install.sh" || die "the uninstall script does not parse"
chmod 0755 "$uninstall_src/post_install.sh"
zipdir "$DIST/MinarchGL.Uninstall.pakz" "$uninstall_src/post_install.sh"
echo "pakz: dist/MinarchGL.Uninstall.pakz ($tags)" >&2

# --- checksums --------------------------------------------------------------
# The installer looks up minarch-gl_<plat>.elf here, and verifies every other
# asset against the manifest, so this file is what makes a download trusted.
sums="$DIST/SHA256SUMS"
: > "$sums"
for asset in "minarch-gl_$PLAT.elf" \
             $(sed -n 's/^\(LIBRETRO_[A-Z0-9_]*\).*/\1/p' "$CORES_LIST" | sed "s|$|_$PLAT.pak.tar.gz|") \
             MinarchGL.Installer.pak.zip MinarchGL.Uninstall.pakz core-list.json; do
	[ -f "$DIST/$asset" ] || die "missing dist/$asset"
	printf '%s  %s\n' "$(sha256 "$DIST/$asset")" "$asset" >> "$sums"
done
echo "checksums: dist/SHA256SUMS ($(wc -l < "$sums" | tr -d ' ') assets)" >&2

say "done"
ls -l "$DIST"
