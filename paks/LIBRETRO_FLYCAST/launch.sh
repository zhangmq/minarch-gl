#!/bin/sh
# minarch-gl core pak: Dreamcast and Naomi on the flycast libretro core.
#
# One tag covers both systems: put the ROMs in "Sega Dreamcast (LIBRETRO_FLYCAST)"
# and "Sega Naomi (LIBRETRO_FLYCAST)".  Flycast detects the system from the
# content, so a single pak (and a single copy of the 26 MB core) serves both;
# this mirrors how NextUI's own paks share one tag across related folders.
# BIOS files for both systems go in "Bios/LIBRETRO_FLYCAST/".
#
# No frontend is bundled here on purpose.  Every pak invokes the bare name
# `minarch.elf`, which resolves through PATH to $SYSTEM_PATH/bin/minarch.elf --
# the switcher in Tools/h700/Minarch GL.pak decides whether that is stock NextUI
# or minarch-gl.  While stock is active the switcher renames this pak to
# *.pak.disabled (it carries the `minarch-gl-only` marker), because stock
# minarch cannot drive a GLES core.

EMU_EXE=flycast
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
export LD_LIBRARY_PATH="$CORES_PATH:$LD_LIBRARY_PATH"
mkdir -p "$BIOS_PATH/$EMU_TAG"
mkdir -p "$SAVES_PATH/$EMU_TAG"
mkdir -p "$CHEATS_PATH/$EMU_TAG"
HOME="$USERDATA_PATH"
cd "$HOME"
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1
