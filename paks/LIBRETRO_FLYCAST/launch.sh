#!/bin/sh
# flycast -- the same shape as NextUI's own Emus/<platform>/<TAG>.pak: the core
# lives in this directory and the frontend is whatever `minarch.elf` resolves
# to.  The MinarchGL Installer decides which build that is; this pak never
# checks, so it cannot disagree with it.
#
# Dreamcast and Naomi share this one tag: Flycast detects the system from the
# content.  ROMs go in "Roms/Sega Dreamcast (LIBRETRO_FLYCAST)" and
# "Roms/Sega Naomi (LIBRETRO_FLYCAST)"; BIOS files in "Bios/LIBRETRO_FLYCAST/".

EMU_EXE=flycast
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
mkdir -p "$BIOS_PATH/$EMU_TAG"
mkdir -p "$SAVES_PATH/$EMU_TAG"
mkdir -p "$CHEATS_PATH/$EMU_TAG"
HOME="$USERDATA_PATH"
cd "$HOME"
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1
