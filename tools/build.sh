#!/bin/sh
# Assemble the build tree and build minarch-gl (and optionally the three cores
# and the pak bundles).
#
#   tools/build.sh                 # frontend only
#   BUILD_CORES=1 tools/build.sh   # frontend + flycast/mupen64plus_next/yabasanshiro + paks
#   BUILD_CORES=flycast tools/build.sh
#
# Layout: the base tree is extracted under .cache/base/<sha>, copied to
# build/tree, and this repository's workspace/ is overlaid on top of it.  The
# paths inside workspace/ are identical to NextUI's, so no rewriting happens --
# that is what keeps workspace/all/minarch-gl/makefile's relative paths valid.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/upstream.lock"

IMG="$TOOLCHAIN_IMAGE@$TOOLCHAIN_DIGEST"
TREE="$ROOT/build/tree"
DIST="$ROOT/dist"
CORE_CACHE="$ROOT/.cache/cores"
DEPS_CACHE="$ROOT/.cache/frontend-deps"
ALL_CORES="flycast mupen64plus_next yabasanshiro"
# Per-platform core builds live in gles-cores/<plat>/, mirroring NextUI's
# workspace/<plat>/ split.  These cores are not upstream ones we maintain, so
# they are not part of the workspace overlay.  h700 is the only platform today.
CORES_PLATFORM=h700
CORES_MK="$ROOT/gles-cores/$CORES_PLATFORM/makefile"

say() { printf '\n== %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

command -v docker >/dev/null || die "docker is required (toolchain image $IMG)"
command -v git >/dev/null || die "git is required (core source pins)"

# Pak archives are plain zips; `zip` is the documented tool but bsdtar writes an
# equivalent archive (same relative paths, unix modes preserved -- launch.sh must
# stay executable when the pak is unzipped on the device).
if command -v zip >/dev/null; then
	zipdir() { zip -qr "$1" "$2"; }
elif command -v bsdtar >/dev/null; then
	zipdir() { bsdtar -a -cf "$1" "$2"; }
else
	die "need zip or bsdtar to build the pak archives"
fi

say "base tree ($BASE_SHA)"
BASE=$(sh "$ROOT/tools/base.sh")

say "assembling $TREE"
# The container writes build products into the tree as root; deleting those from
# the host user fails.  Fall back to a root container for the wipe.
if [ -e "$TREE" ]; then
	rm -rf "$TREE" 2>/dev/null ||
		docker run --rm -v "$ROOT/build:/b" "$IMG" rm -rf /b/tree
fi
mkdir -p "$ROOT/build"
cp -a "$BASE" "$TREE"
cp -a "$ROOT/workspace/." "$TREE/workspace/"
printf '%s\n' "$(printf '%s' "$BASE_SHA" | cut -c1-7)" > "$TREE/workspace/hash.txt"

# The upstream minarch makefile fetches its dependencies into the merged build
# tree (.minarch-gl-build), which is recreated and deleted on every run -- so by
# default every build re-cloned libretro-common and libchdr (minutes, and the
# container's TLS stack is the fragile part) and re-cloned the pinned rcheevos.
# Keep one copy in .cache and seed the tree from it; the recursive make then
# finds the directories and skips its clone targets.
#
# Refresh on demand only (DEPS_REFRESH=1), NEVER on every build: libretro-common
# and libchdr are unpinned upstreams, so re-fetching would silently change the
# binary (rcheevos is pinned by rcheevos.makefile and keyed on that pin here).
say "frontend deps (libretro-common, libchdr, rcheevos)"
if [ "${DEPS_REFRESH:-0}" = "1" ]; then
	rm -rf "$DEPS_CACHE"
fi
mkdir -p "$DEPS_CACHE"
for dep in libretro-common:https://github.com/libretro/libretro-common \
           libchdr:https://github.com/rtissera/libchdr; do
	name=${dep%%:*}
	url=${dep#*:}
	if [ ! -d "$DEPS_CACHE/$name/.git" ]; then
		say "cloning $name (first time; cached afterwards)"
		git -c http.version=HTTP/1.1 clone "$url" "$DEPS_CACHE/$name"
	fi
	printf '%s %s\n' "$name" "$(git -C "$DEPS_CACHE/$name" rev-parse --short HEAD)"
done > "$ROOT/build/frontend-deps.txt"
RCHEEVOS_PIN=$(sed -n 's/^RCHEEVOS_VERSION[[:space:]]*[?:]*=[[:space:]]*//p' \
	"$TREE/workspace/all/minarch/rcheevos.makefile" | head -1)
if [ -n "$RCHEEVOS_PIN" ] && { [ ! -f "$DEPS_CACHE/rcheevos-src/include/rc_client.h" ] ||
		[ "$(git -C "$DEPS_CACHE/rcheevos-src" rev-parse HEAD 2>/dev/null)" != "$RCHEEVOS_PIN" ]; }; then
	say "cloning rcheevos @ $RCHEEVOS_PIN"
	rm -rf "$DEPS_CACHE/rcheevos-src"
	git -c http.version=HTTP/1.1 clone https://github.com/RetroAchievements/rcheevos.git \
		"$DEPS_CACHE/rcheevos-src"
	git -C "$DEPS_CACHE/rcheevos-src" checkout --detach "$RCHEEVOS_PIN"
fi
if [ -f "$DEPS_CACHE/rcheevos-src/include/rc_client.h" ]; then
	printf '%s %s\n' rcheevos "$(git -C "$DEPS_CACHE/rcheevos-src" rev-parse --short HEAD)" \
		>> "$ROOT/build/frontend-deps.txt"
fi
cp -f "$ROOT/build/frontend-deps.txt" "$DEPS_CACHE/versions.txt"
mkdir -p "$TREE/workspace/all/.minarch-gl-deps"
cp -a "$DEPS_CACHE/." "$TREE/workspace/all/.minarch-gl-deps/"

run() {
	# $1: shell snippet inside the container (the workspace tree is at /workspace)
	# Core sources are pre-cloned on the host and copied in with the host uid, so
	# the container's git (running as root) refuses them with "dubious ownership"
	# when a core's MAKE runs `git submodule update`.  Accept the mounted tree for
	# those calls only -- the setting lives in the container's environment, the
	# host git config and the file ownership are left alone.
	#
	# http.version=HTTP/1.1: the frontend build clones libretro-common,
	# rcheevos and libchdr inside this container on every run, and the image's
	# GnuTLS/HTTP2 stack dies with "GnuTLS recv error (-9) / early EOF" on
	# GitHub more often than not (host curl is fine, HTTP/1.1 is fine).
	docker run --rm -v "$TREE/workspace:/workspace" \
		-e PLATFORM=h700 -e UNION_PLATFORM=h700 \
		-e GIT_CONFIG_COUNT=2 -e GIT_CONFIG_KEY_0=safe.directory \
		-e GIT_CONFIG_VALUE_0='*' \
		-e GIT_CONFIG_KEY_1=http.version -e GIT_CONFIG_VALUE_1=HTTP/1.1 \
	"$IMG" /bin/bash -c ". /root/.bashrc && $1"
}

say "building minarch-gl"
run 'cd /workspace/all/minarch-gl && make'

mkdir -p "$DIST"
cp -f "$TREE/workspace/all/minarch-gl/build/h700/minarch-gl.elf" "$DIST/minarch-gl.elf"
echo "frontend: $DIST/minarch-gl.elf ($(md5sum "$DIST/minarch-gl.elf" | cut -d' ' -f1))" >&2

if [ -z "${BUILD_CORES:-}" ]; then
	say "skipping cores (set BUILD_CORES=1 to build the cores and the pak)"
	exit 0
fi

# The installer only ships inside the pak, so it is built with the pak.  It
# lives outside workspace/ in this repository (it is a standalone pak app, not
# an overlay of a NextUI file); the build tree keeps NextUI's layout, so it is
# copied to workspace/installer and built from there.
say "building minarch-gl-installer"
rm -rf "$TREE/workspace/installer"
cp -a "$ROOT/installer" "$TREE/workspace/installer"
run 'cd /workspace/installer && make'

if [ "$BUILD_CORES" = "1" ]; then
	CORES="$ALL_CORES"
else
	CORES="$BUILD_CORES"
fi

pin() {
	# pin <core> <VAR>  -> value from cores/makefile
	sed -n "s/^$1_$2[[:space:]]*=[[:space:]]*//p" "$CORES_MK" | head -1
}

# Some cores need more than a source tree: yabasanshiro's CMake port wants two
# pinned dependency trees plus musashi's generated 68k sources in a directory it
# is pointed at, and links the vendor GL/EGL blobs by absolute path.  Stage both
# into the build tree, from a cache that only has to be filled once.
#
#   .cache/cores/yaba-deps/{libchdr,zlib}   git clones at the pins the core's
#                                           CMakeLists names (see the header of
#                                           build-libretro.sh upstream); musashi
#                                           is generated during the build.
#   .cache/cores/h700-deps/lib/{libEGL.so.1,libmali.so.0}
#                                           the device's GL/EGL libs; the image
#                                           sysroot has libEGL but not the Mali
#                                           blob.  Seeded from $GL_DEPS_SRC
#                                           (default /home/zmq/projects/h700-deps)
#                                           or by hand from the device.
stage_core_deps() {
	say "staging core build files and dependencies"
	# Our core makefile/toolchain/headers are not part of NextUI, so they live in
	# gles-cores/<plat>/ at the repository root; the build tree still expects
	# them at workspace/<plat>/cores/ where NextUI's template looks.
	rm -rf "$TREE/workspace/$CORES_PLATFORM/cores"
	mkdir -p "$TREE/workspace/$CORES_PLATFORM/cores"
	cp -a "$ROOT/gles-cores/$CORES_PLATFORM/." "$TREE/workspace/$CORES_PLATFORM/cores/"
	src="$CORE_CACHE/yaba-deps"
	[ -d "$src" ] || die "missing $src (yabasanshiro's libchdr/zlib sources)"
	mkdir -p "$TREE/workspace/$CORES_PLATFORM/cores"
	rm -rf "$TREE/workspace/$CORES_PLATFORM/cores/yaba-deps"
	cp -a "$src" "$TREE/workspace/$CORES_PLATFORM/cores/yaba-deps"

	gl="$CORE_CACHE/h700-deps"
	if [ ! -f "$gl/lib/libmali.so.0" ] || [ ! -f "$gl/lib/libEGL.so.1" ]; then
		gldeps=${GL_DEPS_SRC:-/home/zmq/projects/h700-deps}
		[ -f "$gldeps/lib/libmali.so.0" ] ||
			die "missing vendor GL libs: put libEGL.so.1 and libmali.so.0 in $gl/lib or set GL_DEPS_SRC"
		mkdir -p "$gl/lib"
		cp -f "$gldeps/lib/libEGL.so.1" "$gldeps/lib/libmali.so.0" "$gl/lib/"
		chmod 0644 "$gl/lib/libmali.so.0" "$gl/lib/libEGL.so.1"
	fi
	rm -rf "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps"
	mkdir -p "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps"
	cp -a "$gl/lib" "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps/lib"
	# The core's CMakeLists adds ${H700_DEPS}/include and expects GLES3/EGL
	# headers there; the image sysroot has most of them but not GLES3/gl3ext.h
	# (nor does the vendor dir), so our copy of that one header is overlaid on
	# top: gles-cores/<plat>/include/ (copied into the tree below).
	if [ -d "$gl/include" ]; then
		cp -a "$gl/include" "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps/include"
	fi
	mkdir -p "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps/include"
	cp -a "$ROOT/gles-cores/$CORES_PLATFORM/include/." "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps/include/"
}

# The NextUI core template clones into workspace/<plat>/cores/src/<core> and
# skips that step when the directory already exists.  Pre-cloning here keeps the
# pins authoritative (no shallow branch tips), avoids the mupen recursive
# submodule trap, and makes repeat builds fast.
preclone() {
	core=$1 repo=$2 branch=$3 hash=$4 sub=$5
	[ -n "$repo" ] || die "no ${core}_REPO in $CORES_MK"
	[ -n "$hash" ] || die "no ${core}_HASH in $CORES_MK"
	d="$CORE_CACHE/$core"
	# The cache is keyed by core name, not by repository: a pin that moved to a
	# different repo (yabasanshiro did, 2026-11-18) would otherwise be built from
	# the old checkout with no warning at all.
	if [ -d "$d/.git" ]; then
		cached_url=$(git -C "$d" remote get-url origin 2>/dev/null || true)
		cached_head=$(git -C "$d" rev-parse HEAD 2>/dev/null || true)
		if [ "$cached_url" != "$repo" ] || [ "$cached_head" != "$hash" ]; then
			say "re-cloning $core (cache had ${cached_head:-nothing} from ${cached_url:-nowhere})"
			rm -rf "$d"
		fi
	fi
	if [ ! -d "$d/.git" ]; then
		say "cloning $core @ $hash"
		mkdir -p "$CORE_CACHE"
		git clone --depth 1 ${branch:+--branch "$branch"} "$repo" "$d"
		git -C "$d" fetch --depth 1 origin "$hash"
		git -C "$d" checkout --detach "$hash"
	fi
	if [ -n "$sub" ]; then
		git -C "$d" submodule update --init --recursive $sub
	fi
	mkdir -p "$TREE/workspace/$CORES_PLATFORM/cores/src"
	rm -rf "$TREE/workspace/$CORES_PLATFORM/cores/src/$core"
	cp -a "$d" "$TREE/workspace/$CORES_PLATFORM/cores/src/$core"
}

stage_core_deps

for c in $CORES; do
	preclone "$c" "$(pin "$c" REPO)" "$(pin "$c" BRANCH)" "$(pin "$c" HASH)" "$(pin "$c" SUBMODULES)"
	say "building core: $c"
	run "cd /workspace && CORE=$c make core"
done

say "assembling the Minarch GL pak"
mkdir -p "$DIST"
stage="$ROOT/build/paks/MinarchGL Installer.pak"
rm -rf "$stage"
mkdir -p "$stage/payload"
cp -a "$ROOT/paks/MinarchGL/." "$stage/"
cp -f "$TREE/workspace/installer/build/h700/minarch-gl-installer.elf" "$stage/minarch-gl-installer.elf"
cp -f "$DIST/minarch-gl.elf" "$stage/payload/minarch-gl.elf"
# Read back by the installer's Status screen (and copied next to the installed
# binary), so a card can say which build of the frontend it is running.
printf '%s\n' "$(date +%Y.%m.%d)" > "$stage/payload/minarch-gl.version"
# A checkout that lost the exec bit (zip round-trip, copy from another OS) would
# otherwise ship a pak that cannot be launched; make the bits explicit.
chmod 0755 "$stage/launch.sh" "$stage/minarch-gl-installer.elf"

# The core paks ride inside the tool pak: the installer installs them into
# Emus/h700 when the user enables GL, so the frontend and the cores it needs
# cannot be installed separately or drift out of step.
for c in $CORES; do
	case "$c" in
		flycast) tag=LIBRETRO_FLYCAST ;;          # Dreamcast + Naomi share one tag
		mupen64plus_next) tag=LIBRETRO_MUPEN64PLUS ;;
		yabasanshiro) tag=LIBRETRO_YABASANSHIRO ;;
		*) die "no pak tag for core $c" ;;
	esac
	core_so="$TREE/workspace/$CORES_PLATFORM/cores/output/${c}_libretro.so"
	[ -f "$core_so" ] || die "missing $core_so (core build failed?)"
	src="$ROOT/paks/$tag"
	[ -d "$src" ] || die "missing pak template $src"
	dst="$stage/payload/$tag.pak"
	cp -a "$src" "$dst"
	cp -f "$core_so" "$dst/${c}_libretro.so"
	chmod 0755 "$dst/launch.sh"
	# What the Status screen shows next to each core: the core name and the pin
	# it was built from.  Written here, at packaging time, because it is the only
	# place that knows both.
	printf '%s %s\n' "$c" "$(git -C "$CORE_CACHE/$c" rev-parse --short=8 HEAD 2>/dev/null || echo unknown)" \
		> "$dst/version"
	# A pak is a binary distribution: the frontend half is PolyForm
	# Noncommercial (LICENSE + NOTICE), the core half is GPLv2 and ships its
	# own licence text.  The pins/source links live in NOTICE.
	cp -f "$ROOT/LICENSE" "$ROOT/NOTICE" "$dst/"
	core_lic="$CORE_CACHE/$c/LICENSE"
	[ -f "$core_lic" ] || die "missing $core_lic (licence text for $c)"
	cp -f "$core_lic" "$dst/LICENSE-$c"
	echo "pak payload: $tag.pak <- $c" >&2
done
cp -f "$ROOT/LICENSE" "$ROOT/NOTICE" "$stage/"

rm -f "$DIST/MinarchGL.Installer.pak.zip"
(cd "$ROOT/build/paks" && zipdir "$DIST/MinarchGL.Installer.pak.zip" "MinarchGL Installer.pak")
echo "pak: $DIST/MinarchGL.Installer.pak.zip" >&2

say "done"
ls -l "$DIST"
