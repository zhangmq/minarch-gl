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

# yabasanshiro: Sega Saturn, GLES hw-render.
# This is OUR fork (github.com/zhangmq/yabause, branch h700, single branch).  The
# libretro port lives in yabause/src/libretro; platform=arm64 selects the AARCH64
# dynarec + FORCE_GLES profile.  The shipped core enables the async VDP renderer
# (YAB_ASYNC_RENDERING) and the core-owned shared EGL context
# (YAB_CORE_SHARED_CONTEXT), which is the hw-render model minarch-gl drives.
# -ldl is required with the glibc 2.28 sysroot: the shared-context path uses
# dlopen/dlsym and libdl is still a separate library there.
yabasanshiro_REPO = https://github.com/zhangmq/yabause
yabasanshiro_BRANCH = h700
yabasanshiro_HASH = 9b36fd6ef8985129d2f829a24d0d3e43f8a621e2
# Relative to src/ (the core template always prefixes `src/`): the libretro port
# sits one level down, inside the yabause checkout this core is fetched as.
yabasanshiro_BUILD_PATH = yabasanshiro/yabause/src/libretro
yabasanshiro_CORE = yabasanshiro_libretro.so
yabasanshiro_MAKE = CFLAGS="-DYAB_ASYNC_RENDERING -DYAB_CORE_SHARED_CONTEXT" \
	CXXFLAGS="-DYAB_ASYNC_RENDERING -DYAB_CORE_SHARED_CONTEXT" \
	LDFLAGS="-ldl" make platform=arm64
# Fetch only the small Android submodule: the default --recursive path would also
# clone ext/eigen, which the libretro build never uses.
yabasanshiro_SUBMODULES = yabause/src/android/oboe

###############################

include ../../all/cores/makefile
