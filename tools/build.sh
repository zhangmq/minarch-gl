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
CORES_MK="$ROOT/workspace/h700/cores/makefile"

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

# The switcher only ships inside the pak, so it is built with the pak.
say "building minarch-switch"
run 'cd /workspace/all/minarch-switch && make'

if [ "$BUILD_CORES" = "1" ]; then
	CORES="$ALL_CORES"
else
	CORES="$BUILD_CORES"
fi

pin() {
	# pin <core> <VAR>  -> value from workspace/h700/cores/makefile
	sed -n "s/^$1_$2[[:space:]]*=[[:space:]]*//p" "$CORES_MK" | head -1
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
	mkdir -p "$TREE/workspace/h700/cores/src"
	rm -rf "$TREE/workspace/h700/cores/src/$core"
	cp -a "$d" "$TREE/workspace/h700/cores/src/$core"
}

for c in $CORES; do
	preclone "$c" "$(pin "$c" REPO)" "$(pin "$c" BRANCH)" "$(pin "$c" HASH)" "$(pin "$c" SUBMODULES)"
	say "building core: $c"
	run "cd /workspace && CORE=$c make core"
done

say "assembling the Minarch GL pak"
mkdir -p "$DIST"
stage="$ROOT/build/paks/Minarch GL.pak"
rm -rf "$stage"
mkdir -p "$stage/payload"
cp -a "$ROOT/paks/MinarchGL/." "$stage/"
cp -f "$TREE/workspace/all/minarch-switch/build/h700/minarch-switch.elf" "$stage/minarch-switch.elf"
cp -f "$DIST/minarch-gl.elf" "$stage/payload/minarch-gl.elf"
# A checkout that lost the exec bit (zip round-trip, copy from another OS) would
# otherwise ship a pak that cannot be launched; make the bits explicit.
chmod 0755 "$stage/launch.sh" "$stage/minarch-switch.elf"

# The core paks ride inside the tool pak: the switcher installs them into
# Emus/h700 when the user enables GL, so the frontend and the cores it needs
# cannot be installed separately or drift out of step.
for c in $CORES; do
	case "$c" in
		flycast) tag=LIBRETRO_FLYCAST ;;          # Dreamcast + Naomi share one tag
		mupen64plus_next) tag=LIBRETRO_MUPEN64PLUS ;;
		yabasanshiro) tag=LIBRETRO_YABASANSHIRO ;;
		*) die "no pak tag for core $c" ;;
	esac
	core_so="$TREE/workspace/h700/cores/output/${c}_libretro.so"
	[ -f "$core_so" ] || die "missing $core_so (core build failed?)"
	src="$ROOT/paks/$tag"
	[ -d "$src" ] || die "missing pak template $src"
	dst="$stage/payload/$tag.pak"
	cp -a "$src" "$dst"
	cp -f "$core_so" "$dst/${c}_libretro.so"
	chmod 0755 "$dst/launch.sh"
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

rm -f "$DIST/Minarch.GL.pak.zip"
(cd "$ROOT/build/paks" && zipdir "$DIST/Minarch.GL.pak.zip" "Minarch GL.pak")
echo "pak: $DIST/Minarch.GL.pak.zip" >&2

say "done"
ls -l "$DIST"
