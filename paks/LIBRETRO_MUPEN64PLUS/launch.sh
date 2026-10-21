#!/bin/sh
# minarch-gl core pak: Nintendo 64 on the mupen64plus-next libretro core.
#
# Put the ROMs in "Nintendo 64 (LIBRETRO_MUPEN64PLUS)".  The tag is the core
# name because the store already has a standalone pak called N64 and two paks
# cannot share a tag.
#
# No frontend is bundled here on purpose.  Every pak invokes the bare name
# `minarch.elf`, which resolves through PATH to $SYSTEM_PATH/bin/minarch.elf --
# the switcher in Tools/h700/Minarch GL.pak decides whether that is stock NextUI
# or minarch-gl.  While stock is active the switcher renames this pak to
# *.pak.disabled (it carries the `minarch-gl-only` marker), because stock
# minarch cannot drive a GLES core.

EMU_EXE=mupen64plus_next
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
