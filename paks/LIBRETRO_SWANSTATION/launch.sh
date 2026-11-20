#!/bin/sh
# swanstation -- PlayStation 1, GLES hw-render. ROMs go in
# "Roms/Sony PlayStation (LIBRETRO_SWANSTATION)"; a real BIOS is optional
# (SwanStation falls back to its built-in OpenBIOS). Put one in
# "Bios/LIBRETRO_SWANSTATION/" if you have it.
#
# The core is a hard fork of DuckStation; see README.md and NOTICE for the
# source pin and licence (GPLv3).
EMU_EXE=swanstation
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
