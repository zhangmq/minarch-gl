# minarch-gl core set (h700 only)
#
# This file replaces NextUI's workspace/h700/cores/makefile inside the
# assembled build tree, so `make core CORE=<name>` only knows the cores this
# repository ships.  The build machinery itself (the TEMPLATE with clone/patch/
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
		-DCMAKE_CXX_STANDARD_LIBRARIES=-lstdc++fs && \
	cmake --build build-h700 --parallel 4 --target yabasanshiro_libretro #
# No submodules are needed: libchdr/zlib come from the staged cache and the
# only entry left in the core's .gitmodules is ext/eigen, which the libretro
# build does not use.  Leaving this empty is safe because tools/build.sh always
# pre-clones the source, so the template's own clone/submodule rule is skipped.
yabasanshiro_SUBMODULES =

###############################

include ../../all/cores/makefile
