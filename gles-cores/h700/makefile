# minarch-gl core set (h700 only)
#
# This file replaces NextUI's workspace/h700/cores/makefile inside the
# assembled build tree, so `make core CORE=<name>` only knows the cores this
# repository ships.  The repository copy lives at gles-cores/<plat>/ (outside
# workspace/): these cores are not upstream ones we maintain, so they are not
# an overlay.  tools/build.sh copies the directory to workspace/<plat>/cores/
# in the tree; per-platform files for other SoCs would live in their own
# gles-cores/<plat>/.  The build machinery itself (the TEMPLATE with clone/patch/
# output rules) comes from NextUI's workspace/all/cores/makefile, which the base
# tree provides -- see the include at the bottom.
#
# Only h700 is maintained here: yabause is our own fork and only has an h700
# branch, and the flycast / mupen64plus-next pins were picked for this SoC.
# Adding a platform means adding workspace/<plat>/cores/makefile next to this
# file.

CORES= flycast
CORES+= mupen64plus_next
CORES+= yabasanshiro
CORES+= swanstation
CORES+= ppsspp

###############################

# flycast: Dreamcast / NAOMI, GLES hw-render.
# Upstream is flyinghead/flycast, NOT libretro/flycast (that one is a deprecated
# fork: no GLES, no pvr message queue).  Pinned to v2.0, the build picked for
# H700 after the 2026-09-10/11 version sweep.
flycast_REPO = https://github.com/flyinghead/flycast
flycast_HASH = aa97a6d64fb47d3ce0febaa575b26d975dd916e4
# LIBRETRO=ON only needs libchdr (SDL/luabridge/breakpad are standalone-only), but
# libchdr's own submodules (zlib-1.2.11, lzma) must come in recursively -- the shared
# template's SUBMODULES branch updates non-recursively, so MAKE re-runs it.
flycast_SUBMODULES = core/deps/libchdr
flycast_CORE = build-h700/flycast_libretro.so
# -DCMAKE_CXX_STANDARD=17 is required here: v2.0's CMakeLists sets no standard and the
# toolchain gcc 8.3 defaults to gnu++14.  No source patches needed on this
# toolchain: libzip's arc4random probe fails naturally against the glibc 2.28 sysroot.
flycast_MAKE = git submodule update --init --recursive core/deps/libchdr && \
	cmake -S . -B build-h700 -DCMAKE_BUILD_TYPE=Release -DLIBRETRO=ON \
		-DUSE_GLES=ON -DUSE_GLES2=OFF -DUSE_VULKAN=OFF -DUSE_OPENMP=OFF \
		-DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
		-DCMAKE_C_FLAGS="-fomit-frame-pointer -ffast-math -mtune=cortex-a53 -mcpu=cortex-a53 -march=armv8-a -fno-common" \
		-DCMAKE_CXX_FLAGS="-fomit-frame-pointer -ffast-math -mtune=cortex-a53 -mcpu=cortex-a53 -march=armv8-a -fno-common -D_GNU_SOURCE" && \
	cmake --build build-h700 --target flycast_libretro -- $(PROCS)

# mupen64plus-next: Nintendo 64, GLES3 hardware render.  Chosen for H700 over
# parallel-n64 after the 2026-09-16 survey: mupen64plus-next is the compatibility
# leader on H700-class devices, while parallel-n64's ParaLLEl-RDP (LLE) is too
# heavy for this SoC and only pays off for titles with severe graphics bugs.
# ARCH/WITH_DYNAREC must be forced: the Makefile defaults ARCH to `uname -m`,
# which is the x86_64 build host, not the target.  platform=unix + FORCE_GLES3=1
# selects the GLES3 renderer (GL_LIB := -lGLESv2), i.e. the same libretro
# hw-render path minarch-gl drives for flycast.
mupen64plus_next_REPO = https://github.com/libretro/mupen64plus-libretro-nx
mupen64plus_next_HASH = 6752836de8b224febfd5708444755b77712ac939
# FRESH-CLONE CAVEAT: upstream's develop tree carries gitlinks
# (mupen64plus-rsp-paraLLEl, ...-video-paraLLEl) but ships no .gitmodules, so the
# template's `git submodule update --init --recursive` aborts with "No url found for
# submodule path .../lightning/gnulib".  Those trees are the Vulkan paraLLEl path and
# are NOT needed for the GLES build.  tools/build.sh pre-clones the source into
# workspace/h700/cores/src/<core>, which makes the template skip its own clone step.
mupen64plus_next_CORE = mupen64plus_next_libretro.so
mupen64plus_next_MAKE = make platform=unix FORCE_GLES3=1 ARCH=aarch64 WITH_DYNAREC=aarch64
# Left empty on purpose: the template then takes its `--recursive` branch, which is
# exactly what must be avoided on a fresh clone -- pre-clone instead (see above).
mupen64plus_next_SUBMODULES =

# yabasanshiro: Sega Saturn + ST-V, GLES hw-render.
#
# The core lives in zhangmq/yabasanshiro-pirry on the h700-libretro branch.  Its
# libretro port is a CMake project now (the old in-tree makefile is gone), and
# tools/build.sh stages its dependencies from .cache -- see yabasanshiro_MAKE.
#
# We drive CMake ourselves rather than through the core's build-libretro.sh:
# that script exists for the core's own developers (it git-clones libchdr and
# zlib and takes its compiler from an optional toolchain file), while this build
# has to stay hermetic, use the image's gcc 8.3 like the other cores, and keep
# the same tuning flags.  Our toolchain file is next to this makefile.
#
# The two <filesystem> translation units the CMake source list added
# (thr-linux.cpp, PlayRecorder.cpp) need libstdc++fs with gcc < 9; the upstream
# CMakeLists does not link it yet, so it is added here until the core carries it
# (see .docs/FEEDBACK-YABA-BUILD-20261118.md).
yabasanshiro_REPO = https://github.com/zhangmq/yabasanshiro-pirry
yabasanshiro_BRANCH = h700-libretro
yabasanshiro_HASH = eb32d3f9667ca1e00afcf633881c9484624b8f6f
yabasanshiro_BUILD_PATH = yabasanshiro
yabasanshiro_CORE = build-h700/yabasanshiro_libretro.so
# musashi's generated m68k sources (m68kops.c / m68kop*.c) are a pure function
# of m68k_in.c: build the generator with the *host* compiler (the container has
# gcc 13) and run it into the staged dependency tree.  GCC_VERSION=8 matches the
# image's 8.3: the engine picks <experimental/filesystem> below gcc 9, and
# -lstdc++fs is what resolves those symbols.
# yabasanshiro's tuning: the toolchain's MGL_TUNE, spelled out per core so one
# flag can be flipped for an A/B without touching the other cores.
#
# yaba does NOT use -ffast-math (2026-11-19).  With it, Daytona USA's demo
# mountain texture shows a radial artefact; without it the texture is correct.
# Everything else stays: -O3, the A53 tuning, -fno-strict-aliasing.  The other
# two cores keep their own flags (all three used to share MGL_TUNE).  Both yaba
# variants link libmali directly instead of the libEGL shim (which exports no
# symbols on this SoC).
#
# The rest of the bundle was not bisected: if the lost float throughput ever
# matters, try -ffast-math with -fno-reciprocal-math / -fno-finite-math-only
# first -- the artefact looks like a reciprocal/rounding change on the big
# textured quads rather than a NaN-handling one.
yaba_flags = -O3 -DNDEBUG -fomit-frame-pointer -fno-strict-aliasing \
	-march=armv8-a+crc+fp+simd -mcpu=cortex-a53 -mtune=cortex-a53 -D_GNU_SOURCE

yabasanshiro_MAKE = rm -rf deps build-h700 && \
	cp -a /workspace/h700/cores/yaba-deps deps && \
	mkdir -p deps/musashi build-h700/.tools && \
	cc -O2 -o build-h700/.tools/m68kmake yabause/src/musashi/m68kmake.c && \
	./build-h700/.tools/m68kmake deps/musashi/ yabause/src/musashi/m68k_in.c && \
	cmake -S yabause/src/libretro -B build-h700 \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=/workspace/h700/cores/yabasanshiro-toolchain.cmake \
		-DYAB_DEPS_CACHE=$$PWD/deps \
		-DH700_DEPS=/workspace/h700/cores/h700-deps \
		-DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
		-DCMAKE_CXX_STANDARD_LIBRARIES=-lstdc++fs \
		-DCMAKE_SHARED_LINKER_FLAGS="-Wl,--no-as-needed" \
		-DCMAKE_C_FLAGS="$(yaba_flags)" -DCMAKE_CXX_FLAGS="$(yaba_flags)" && \
	cmake --build build-h700 --parallel 4 --target yabasanshiro_libretro #
# No submodules are needed: libchdr/zlib come from the staged cache and the
# only entry left in the core's .gitmodules is ext/eigen, which the libretro
# build does not use.  Leaving this empty is safe because tools/build.sh always
# pre-clones the source, so the template's own clone/submodule rule is skipped.
yabasanshiro_SUBMODULES =
# The libretro port links libchdr and zlib, which its own build-libretro.sh
# clones; here they are staged from .cache so the build is hermetic.  When the
# cache is empty (a fresh checkout, CI) tools/build.sh clones these pins.
yaba_libchdr_REPO = https://github.com/devmiyax/libchdr
yaba_libchdr_HASH = 074ff1614f2a685f2b5a95b0e788bff6297d5680
yaba_zlib_REPO = https://github.com/madler/zlib
yaba_zlib_HASH = 50893291621658f355bc5b4d450a8d06a563053d

# swanstation: PlayStation 1, GLES 3.0+ hw-render, GPLv3.
#
# The libretro core lives in libretro/swanstation (a hard fork of DuckStation)
# and embeds OpenBIOS, so it boots without a BIOS image.  AArch64 JIT is
# supported.  Unlike the other cores it does not link GLES/EGL at all: GLAD
# resolves every GL entry point through SET_HW_RENDER's get_proc_address, so
# there is no libmali/GLESv2 shim and no DT_NEEDED on the vendor driver.
#
# CPU_ARCH must be forced: Makefile.libretro otherwise detects the build host's
# uname (x86_64).  Its own Makefile adds -std=gnu++17; the tuning flags are
# passed as environment variables so the Makefile's += (includes, std) survives.
swanstation_REPO = https://github.com/libretro/swanstation
swanstation_HASH = 631f0fb31b809e0f889d7c551fe695788e39c1d3
swanstation_CORE = swanstation_libretro.so
swanstation_cflags = -O3 -DNDEBUG -fomit-frame-pointer -fno-strict-aliasing \
	-march=armv8-a+crc+fp+simd -mcpu=cortex-a53 -mtune=cortex-a53 -D_GNU_SOURCE
swanstation_MAKE = CXXFLAGS="$(swanstation_cflags)" CFLAGS="$(swanstation_cflags)" \
	make -f Makefile.libretro platform=unix CPU_ARCH=aarch64
swanstation_SUBMODULES =

# ppsspp: PlayStation Portable, GLES 2/3 hw-render.
#
# The libretro port is in the upstream tree under libretro/ and needs the
# submodules (16 of them: libpng, ffmpeg, glslang, ...), so build.sh preclones
# and updates them before the template runs.  The platform is arm64-gles, not
# the template's h700: PPSSPP's Makefile only knows its own platform names.
#
# The four build fixes live in patches/ppsspp.patch: no X11/Vulkan define,
# the gcc 8.3 copysign ICE workaround, the IndependentTask CTAD replacement,
# the libpng arm64 NEON sources and the TransformUnit -O1 override.
#
# glshim is a link-time-only shim: the H700 sysroot has libEGL/libGLESv2
# stubs, but the vendor entry points are all in libmali.so.0.  The symlinks
# make -lGLESv2/-lEGL resolve to libmali; at runtime the device's libmali
# provides the symbols (DT_NEEDED is libmali.so.0).  -lstdc++fs is for the
# armips <filesystem> use under gcc 8.
ppsspp_REPO = https://github.com/hrydgard/ppsspp
ppsspp_HASH = 2b5715ece170ee4154dba3360807204a0c4b6bdd
ppsspp_BUILD_PATH = ppsspp/libretro
ppsspp_CORE = ppsspp_libretro.so
# Only the submodules the libretro build actually reads.  A blank value would
# make build.sh fall back to `--recursive` over everything, including
# pspautotests/frametests (large test trees) and the Android oboe backend.
ppsspp_SUBMODULES = ext/OpenXR-SDK ext/SPIRV-Cross ext/aemu_postoffice ext/armips ext/cpu_features ext/discord-rpc ext/freetype ext/glslang ext/libadrenotools ext/libchdr ext/lua ext/miniupnp ext/nanosvg ext/rapidjson ext/rcheevos ext/zstd libretro/libretro-common
ppsspp_flags = -O3 -DNDEBUG -fomit-frame-pointer -fno-strict-aliasing \
	-fno-tree-vectorize -march=armv8-a+crc+fp+simd -mcpu=cortex-a53 \
	-mtune=cortex-a53 -D_GNU_SOURCE
ppsspp_MAKE = mkdir -p ../glshim && \
	ln -sf /workspace/h700/cores/h700-deps/lib/libmali.so.0 ../glshim/libGLESv2.so && \
	ln -sf /workspace/h700/cores/h700-deps/lib/libmali.so.0 ../glshim/libEGL.so && \
	LIBRARY_PATH=../glshim CXXFLAGS="$(ppsspp_flags)" CFLAGS="$(ppsspp_flags)" \
	LDFLAGS="-L../glshim -lstdc++fs -Wl,--no-as-needed" \
	make -f Makefile platform=arm64-gles

###############################

include ../../all/cores/makefile
