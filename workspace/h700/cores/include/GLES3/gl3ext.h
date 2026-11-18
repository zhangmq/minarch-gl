/*
 * GLES3/gl3ext.h -- compatibility shim for this build environment.
 *
 * The Khronos GLES 3 headers moved the extension entry points into gl3.h,
 * gl31.h and gl32.h, and kept gl3ext.h as an (essentially empty) compatibility
 * header.  Neither the NextUI toolchain sysroot nor the vendor GL headers we
 * stage carry it at all, yet some vendored code in the core tree
 * (yabause/src/nanovg/nanovg_osdcore.c) still includes it.
 *
 * This file reproduces the empty header.  A translation unit that really needs
 * an extension entry point has to include <GLES3/gl32.h> itself; if that turns
 * out to be the case here, the compiler will say so and this file should gain
 * nothing but a note.
 *
 * tools/build.sh copies this directory over the staged ${H700_DEPS}/include, so
 * the file lands where the core's CMakeLists already looks.
 */
#ifndef __gl3ext_h_
#define __gl3ext_h_

#include <GLES3/gl3platform.h>

#endif /* __gl3ext_h_ */
