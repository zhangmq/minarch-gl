#!/bin/sh
# Assemble the build tree, build minarch-gl, and stage what a release ships.
#
#   tools/build.sh                  # frontend only            -> dist/minarch-gl.elf
#   BUILD_INSTALLER=1 tools/build.sh # + the installer          -> dist/minarch-gl-installer.elf
#                                     and build/paks/MinarchGL Installer.pak/
#   BUILD_CORES=1 tools/build.sh    # + flycast/mupen64plus_next/yabasanshiro/swanstation
#                                     and build/paks/<TAG>.pak/ for each
#   BUILD_CORES=flycast ...         # one core only (partial staging; the release
#                                   # script refuses a partial manifest unless asked)
#
# tools/release.sh turns the staged output into the assets of one release; this
# script only compiles and arranges files.
#
# Layout: the base tree is extracted under .cache/base/<sha>, copied to
# build/tree, and this repository's workspace/ is overlaid on top of it.  The
# paths inside workspace/ are identical to NextUI's, so no rewriting happens --
# that is what keeps workspace/all/minarch-gl/makefile's relative paths valid.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/upstream.lock"
. "$ROOT/tools/lib.sh"

IMG="$TOOLCHAIN_IMAGE@$TOOLCHAIN_DIGEST"
TREE="$ROOT/build/tree"
DIST="$ROOT/dist"
PAKS="$ROOT/build/paks"
CORE_CACHE="$ROOT/.cache/cores"
DEPS_CACHE="$ROOT/.cache/frontend-deps"
ALL_CORES="flycast mupen64plus_next yabasanshiro swanstation ppsspp"
# Per-platform core builds live in gles-cores/<plat>/, mirroring NextUI's
# workspace/<plat>/ split.  These cores are not upstream ones we maintain, so
# they are not part of the workspace overlay.  h700 is the only platform today.
CORES_PLATFORM=h700
CORES_MK="$ROOT/gles-cores/$CORES_PLATFORM/makefile"
# The core table: tag, core name and the ROM folders each tag owns.  Release
# manifests and the pak staging both read it, so the three cannot drift.
CORES_LIST="$ROOT/gles-cores/$CORES_PLATFORM/cores.list"
CORES_PATCHES="$ROOT/gles-cores/$CORES_PLATFORM/patches"

command -v docker >/dev/null || die "docker is required (toolchain image $IMG)"
command -v git >/dev/null || die "git is required (core source pins)"

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

if [ -z "${BUILD_INSTALLER:-}" ] && [ -z "${BUILD_CORES:-}" ]; then
	say "skipping the installer and cores (set BUILD_INSTALLER=1 or BUILD_CORES=1)"
	exit 0
fi

# The installer only ships inside the pak, so it is built with it.  It lives
# outside workspace/ in this repository (it is a standalone pak app, not an
# overlay of a NextUI file); the build tree keeps NextUI's layout, so it is
# copied to workspace/installer and built from there.
say "building minarch-gl-installer"
rm -rf "$TREE/workspace/installer"
cp -a "$ROOT/installer" "$TREE/workspace/installer"
run 'cd /workspace/installer && make'
cp -f "$TREE/workspace/installer/build/h700/minarch-gl-installer.elf" "$DIST/minarch-gl-installer.elf"
echo "installer: $DIST/minarch-gl-installer.elf ($(md5sum "$DIST/minarch-gl-installer.elf" | cut -d' ' -f1))" >&2

# The lite installer pak: the tool and nothing else.  Everything it installs
# comes from a release, so this is what the store (or a side-load) needs -- and
# the only thing that has to be re-published when a core or the frontend
# changes.
say "staging the MinarchGL Installer pak"
installer_pak="$PAKS/MinarchGL Installer.pak"
rm -rf "$installer_pak"
mkdir -p "$installer_pak"
cp -a "$ROOT/paks/MinarchGL/." "$installer_pak/"
cp -f "$DIST/minarch-gl-installer.elf" "$installer_pak/minarch-gl-installer.elf"
cp -f "$ROOT/LICENSE" "$ROOT/NOTICE" "$installer_pak/"
# A checkout that lost the exec bit (zip round-trip, copy from another OS) would
# otherwise ship a pak that cannot be launched; make the bits explicit.
chmod 0755 "$installer_pak/launch.sh" "$installer_pak/minarch-gl-installer.elf"

if [ -z "${BUILD_CORES:-}" ]; then
	say "skipping cores (set BUILD_CORES=1 to build the cores)"
	exit 0
fi

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
	# A fresh checkout (CI included) has no cache: fetch the two pins the core's
	# own build-libretro.sh would clone, so the build is reproducible from the
	# repository alone.
	for dep in libchdr zlib; do
		repo=$(pin yaba "${dep}_REPO")
		hash=$(pin yaba "${dep}_HASH")
		[ -n "$repo" ] && [ -n "$hash" ] || die "no yaba ${dep} pin in $CORES_MK"
		if [ ! -d "$src/$dep/.git" ] ||
		   [ "$(git -C "$src/$dep" rev-parse HEAD 2>/dev/null)" != "$hash" ]; then
			say "cloning yaba-$dep @ $(short_pin "$hash")"
			rm -rf "$src/$dep"
			mkdir -p "$src"
			git clone --depth 1 "$repo" "$src/$dep"
			git -C "$src/$dep" fetch --depth 1 origin "$hash"
			git -C "$src/$dep" checkout --detach "$hash"
		fi
	done
	mkdir -p "$TREE/workspace/$CORES_PLATFORM/cores"
	rm -rf "$TREE/workspace/$CORES_PLATFORM/cores/yaba-deps"
	cp -a "$src" "$TREE/workspace/$CORES_PLATFORM/cores/yaba-deps"

	gl="$CORE_CACHE/h700-deps"
	if [ ! -f "$gl/lib/libmali.so.0" ] || [ ! -f "$gl/lib/libEGL.so.1" ]; then
		gldeps=${GL_DEPS_SRC:-/home/zmq/projects/h700-deps}
		if [ -f "$gldeps/lib/libmali.so.0" ]; then
			mkdir -p "$gl/lib"
			cp -f "$gldeps/lib/libEGL.so.1" "$gldeps/lib/libmali.so.0" "$gl/lib/"
			chmod 0644 "$gl/lib/libmali.so.0" "$gl/lib/libEGL.so.1"
		fi
	fi
	rm -rf "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps"
	mkdir -p "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps/lib"
	if [ -d "$gl/lib" ]; then
		cp -a "$gl/lib/." "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps/lib/"
	fi
	# The driver is a device blob and is not in the repository.  Linking only
	# needs the SONAME (the core's gl* references stay undefined and are resolved
	# by the device's libmali at run time), so a clean checkout -- CI -- gets a
	# stub with just that name.  --no-as-needed (in the core makefile) keeps the
	# DT_NEEDED record even though the stub exports nothing.
	if [ ! -f "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps/lib/libmali.so.0" ]; then
		say "no vendor libmali in $gl/lib: generating a link-time stub (the device provides the driver)"
		run 'printf "int mgl_link_stub;\n" > /tmp/mgl-stub.c && \
			aarch64-nextui-linux-gnu-gcc -shared -fPIC -Wl,-soname,libmali.so.0 \
			-o /workspace/'"$CORES_PLATFORM"'/cores/h700-deps/lib/libmali.so.0 /tmp/mgl-stub.c'
	fi
	# The H700 port's CMakeLists links ${H700_DEPS}/lib/libEGL.so.1 *and*
	# libmali.so.0.  On this SoC libEGL.so.1 is a 4 KB shim that exports no
	# symbols at all -- every egl*/gl* entry point lives in libmali.so.0, and
	# the core imports no egl* symbol -- so the only thing that entry adds is a
	# DT_NEEDED record.  Point it at libmali, so the core links the driver
	# directly, the way the hand-built core the user tested (Ubuntu gcc 12,
	# NEEDED libmali alone) does; with the libEGL entry the yaba VDP output
	# showed a radial artefact on Daytona USA's mountain texture (2026-11-19).
	# The symlink keeps the CMakeLists' absolute path valid, and the linker
	# reads libmali's SONAME, so the duplicate entry collapses into one.
	ln -sf libmali.so.0 "$TREE/workspace/$CORES_PLATFORM/cores/h700-deps/lib/libEGL.so.1"
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
	if [ "$sub" = "ALL" ]; then
		git -C "$d" submodule update --init --recursive
	elif [ -n "$sub" ]; then
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

# One flat pak per core, exactly like NextUI's own Emus/<platform>/<TAG>.pak
# (launch.sh + default.cfg + the .so), plus the `version` file this project adds
# so the installer can tell which build is installed.  tools/release.sh packs
# these into <TAG>_<platform>.pak.tar.gz.
say "staging core paks"
while read -r tag core folders; do
	case "$tag" in LIBRETRO_*) ;; *) continue ;; esac
	case " $CORES " in *" $core "*) ;; *) continue ;; esac
	src="$ROOT/paks/$tag"
	[ -d "$src" ] || die "missing pak template $src"
	core_so="$TREE/workspace/$CORES_PLATFORM/cores/output/${core}_libretro.so"
	[ -f "$core_so" ] || die "missing $core_so (core build failed?)"
	dst="$PAKS/$tag.pak"
	rm -rf "$dst"
	mkdir -p "$dst"
	cp -a "$src/." "$dst/"
	cp -f "$core_so" "$dst/${core}_libretro.so"
	# PPSSPP needs its version-matched asset bundle (fonts, flash0,
	# ppge_atlas, compat.ini).  Ship the pinned source's assets unmodified;
	# the H700 4-line delta is the pak's separate compat.ini, copied to the
	# user override path by launch.sh.
	if [ "$core" = "ppsspp" ]; then
		[ -d "$CORE_CACHE/ppsspp/assets" ] || die "missing $CORE_CACHE/ppsspp/assets"
		cp -a "$CORE_CACHE/ppsspp/assets" "$dst/assets"
	fi
	chmod 0755 "$dst/launch.sh"
	# What the installer compares against the manifest: the core name and its
	# pin (commit + patch hash, see core_pin).  Written at packaging time
	# because this is the only place that knows both.
	printf '%s %s\n' "$core" "$(core_pin "$core" "$(pin "$core" HASH)" "$CORES_PATCHES")" > "$dst/version"
	# A pak is a binary distribution: the frontend half is PolyForm
	# Noncommercial (LICENSE + NOTICE), the core half is GPLv2 and ships its
	# own licence text.  The pins/source links live in NOTICE.
	cp -f "$ROOT/LICENSE" "$ROOT/NOTICE" "$dst/"
	core_lic="$CORE_CACHE/$core/LICENSE"
	[ -f "$core_lic" ] || core_lic="$CORE_CACHE/$core/LICENSE.TXT"
	[ -f "$core_lic" ] || die "missing $core_lic (licence text for $core)"
	cp -f "$core_lic" "$dst/LICENSE-$core"
	echo "staged: build/paks/$tag.pak <- $core $(core_pin "$core" "$(pin "$core" HASH)" "$CORES_PATCHES")" >&2
done < "$CORES_LIST"

say "done"
ls -l "$DIST" "$PAKS"
