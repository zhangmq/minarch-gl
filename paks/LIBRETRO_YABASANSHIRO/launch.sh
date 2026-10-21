#!/bin/sh
# minarch-gl core pak: Sega Saturn and Sega Titan Video (ST-V) on yabasanshiro.
#
# One tag covers both systems: put the ROMs in
# "Sega Saturn (LIBRETRO_YABASANSHIRO)" and "Sega Titan Video (LIBRETRO_YABASANSHIRO)".
# The core detects ST-V content by itself; Saturn's saturn_bios.bin and ST-V's
# stvbios.zip both go in "Bios/LIBRETRO_YABASANSHIRO/".  This mirrors how the
# flycast pak shares one tag between Dreamcast and Naomi.
#
# The tag is the core name because the store already has a standalone Saturn pak
# called SS and two paks cannot share a tag.
#
# No frontend is bundled here on purpose.  Every pak invokes the bare name
# `minarch.elf`, which resolves through PATH to $SYSTEM_PATH/bin/minarch.elf --
# the switcher in Tools/h700/Minarch GL.pak decides whether that is stock NextUI
# or minarch-gl.  While stock is active the switcher renames this pak to
# *.pak.disabled (it carries the `minarch-gl-only` marker), because stock
# minarch cannot drive a GLES core.

EMU_EXE=yabasanshiro
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
