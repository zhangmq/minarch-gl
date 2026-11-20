#!/bin/sh
# flycastrophe -- Flycast with Deadcast's speed work (see the pak's NOTICE for
# the pin).  Same shape as NextUI's own Emus/<platform>/<TAG>.pak: the core lives
# in this directory and the frontend is whatever `minarch.elf` resolves to.  The
# MinarchGL Installer decides which build that is; this pak never checks, so it
# cannot disagree with it.
#
# A separate tag from flycast on purpose: it trades accuracy for speed and its
# core options default to the fast path, so the two must not share ROM folders
# or save states.  Dreamcast and Naomi share this one tag: Flycast detects the
# system from the content.  ROMs go in "Roms/Sega Dreamcast
# (LIBRETRO_FLYCASTROPHE)" and "Roms/Sega Naomi (LIBRETRO_FLYCASTROPHE)"; BIOS
# files in "Bios/LIBRETRO_FLYCASTROPHE/".

EMU_EXE=flycastrophe
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
