#!/bin/sh

# minarch-gl pak launcher.
# This pak carries its own frontend (minarch-gl.elf) next to the core, so it
# never touches $SYSTEM_PATH/bin/minarch.elf: stock NextUI stays intact, and
# removing the pak folder is a complete rollback.
# Everything else is the stock NextUI pak boilerplate: the launcher exports
# BIOS_PATH/SAVES_PATH/CHEATS_PATH/USERDATA_PATH/LOGS_PATH and passes the ROM
# as $1.

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
"$CORES_PATH/minarch-gl.elf" "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1
