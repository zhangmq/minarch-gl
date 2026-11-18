# CMake toolchain for our h700 libretro cores, driven from this repository.
#
# The NextUI toolchain image already carries everything needed: the cross
# compiler (aarch64-nextui-linux-gnu-*, Debian's gcc 8.3 with a glibc 2.28
# sysroot) and the SDL2/GL headers.  We point CMake at it directly instead of
# using a core's own build script, for three reasons:
#
#   * hermetic builds -- the core scripts fetch their dependencies from the
#     network; here the sources are staged from .cache by tools/build.sh.
#   * one set of tuning flags for every core (see MGL_TUNE below).
#   * the core script's compiler choice would be whatever its author used; the
#     image's 8.3 is what the frontend and the other two cores are built with.
#
# Usage (see workspace/h700/cores/makefile):
#   cmake -S <core>/src -B build -DCMAKE_TOOLCHAIN_FILE=<this file>
#
# Nothing here is yabasanshiro-specific except the note in MGL_TUNE.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(MGL_CROSS /opt/aarch64-nextui-linux-gnu)
set(MGL_SYSROOT ${MGL_CROSS}/aarch64-nextui-linux-gnu/libc)

set(CMAKE_C_COMPILER   ${MGL_CROSS}/bin/aarch64-nextui-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${MGL_CROSS}/bin/aarch64-nextui-linux-gnu-g++)
set(CMAKE_ASM_COMPILER ${MGL_CROSS}/bin/aarch64-nextui-linux-gnu-gcc)
set(CMAKE_SYSROOT ${MGL_SYSROOT})

# The H700 is an in-order Cortex-A53, so -mcpu/-mtune are not cosmetic.  Glibc
# 2.28 is the image's sysroot: keeping the binary on it is what makes the pak run
# on any current BaseOS.
#
# No -ffast-math here (2026-11-19): with it, yabasanshiro renders Daytona USA's
# demo mountain texture with a radial artefact, and the measured cost of
# dropping it is ~0.4% CPU per frame.  yaba passes its own -DCMAKE_C_FLAGS from
# gles-cores/<plat>/makefile, so these INIT values are only the fallback; flycast
# still carries -ffast-math in its own flag list (no artefacts seen there).
set(MGL_TUNE "-O3 -DNDEBUG -fomit-frame-pointer -fno-strict-aliasing \
-march=armv8-a+crc+fp+simd -mcpu=cortex-a53 -mtune=cortex-a53")

set(CMAKE_C_FLAGS_INIT   "${MGL_TUNE} -D_GNU_SOURCE")
set(CMAKE_CXX_FLAGS_INIT "${MGL_TUNE} -D_GNU_SOURCE")
set(CMAKE_ASM_FLAGS_INIT "-march=armv8-a+crc+fp+simd -mcpu=cortex-a53 -mtune=cortex-a53")
# No CMAKE_SHARED_LINKER_FLAGS_INIT: a shared library may carry undefined
# symbols anyway, so --allow-shlib-undefined only matters when linking an
# executable against such a library (the standalone build, not this core).

set(CMAKE_FIND_ROOT_PATH ${MGL_SYSROOT}/usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
