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
ALL_CORES="flycast mupen64plus_next yabasanshiro"
CORES_MK="$ROOT/workspace/h700/cores/makefile"

say() { printf '\n== %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

command -v docker >/dev/null || die "docker is required (toolchain image $IMG)"
command -v git >/dev/null || die "git is required (core source pins)"

say "base tree ($BASE_SHA)"
BASE=$(sh "$ROOT/tools/base.sh")

say "assembling $TREE"
rm -rf "$TREE"
mkdir -p "$ROOT/build"
cp -a "$BASE" "$TREE"
cp -a "$ROOT/workspace/." "$TREE/workspace/"
printf '%s\n' "$(printf '%s' "$BASE_SHA" | cut -c1-7)" > "$TREE/workspace/hash.txt"

run() {
	# $1: shell snippet inside the container (workdir /root/workspace)
	docker run --rm -v "$TREE:/root/workspace" \
		-e PLATFORM=h700 -e UNION_PLATFORM=h700 \
		"$IMG" /bin/bash -c ". ~/.bashrc && $1"
}

say "building minarch-gl"
run 'cd /root/workspace/all/minarch-gl && make'

mkdir -p "$DIST"
cp -f "$TREE/workspace/all/minarch-gl/build/h700/minarch-gl.elf" "$DIST/minarch-gl.elf"
echo "frontend: $DIST/minarch-gl.elf ($(md5sum "$DIST/minarch-gl.elf" | cut -d' ' -f1))" >&2

if [ -z "${BUILD_CORES:-}" ]; then
	say "skipping cores (set BUILD_CORES=1 to build the cores and the paks)"
	exit 0
fi

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
	run "cd /root/workspace && CORE=$c make core"
done

say "assembling paks"
mkdir -p "$DIST"
for c in $CORES; do
	case "$c" in
		flycast) tags="DC NAOMI" ;;
		mupen64plus_next) tags="N64" ;;
		yabasanshiro) tags="SATURN" ;;
		*) continue ;;
	esac
	core_so="$TREE/workspace/h700/cores/output/${c}_libretro.so"
	[ -f "$core_so" ] || die "missing $core_so (core build failed?)"
	for tag in $tags; do
		stage="$ROOT/build/paks/$tag.pak"
		rm -rf "$stage"
		mkdir -p "$stage"
		cp -a "$ROOT/paks/$tag/." "$stage/"
		cp -f "$DIST/minarch-gl.elf" "$stage/minarch-gl.elf"
		cp -f "$core_so" "$stage/${c}_libretro.so"
		rm -f "$DIST/$tag.pak.zip"
		(cd "$ROOT/build/paks" && zip -qr "$DIST/$tag.pak.zip" "$tag.pak")
		echo "pak: $DIST/$tag.pak.zip" >&2
	done
done

say "done"
ls -l "$DIST"
