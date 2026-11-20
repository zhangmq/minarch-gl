#!/bin/sh
# ppsspp -- PlayStation Portable, GLES hw-render.  ROMs go in
# "Roms/PlayStation Portable (LIBRETRO_PPSSPP)".
#
# The core's system directory is Bios/<TAG>/; PPSSPP wants its assets under
# Bios/<TAG>/PPSSPP/ (compat.ini, ppge_atlas.zim, flash0, ...).  The pak ships
# the version-matched upstream assets and seeds them on first launch; a user
# who replaces fonts/assets keeps their copy.
#
# The 30 fps GoW entries are a per-user compat diff, not the stock asset: it is
# copied to Saves/<TAG>/SYSTEM/compat.ini, which PPSSPP merges on top of
# Bios/<TAG>/PPSSPP/compat.ini.  Assets stay factory default.
EMU_EXE=ppsspp
CORES_PATH=$(dirname "$0")
EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"

# The core tag is the ROM folder suffix, which is what minarch hands to the
# core.  Normally it equals the pak tag; deriving it also keeps the pak usable
# when testing against an existing Roms/... (PSP) folder.
TAG="$EMU_TAG"
ROM_DIR=$(dirname "$ROM")
case "$ROM_DIR" in
	*"("*")") TAG=${ROM_DIR##*\(}; TAG=${TAG%\)} ;;
esac

mkdir -p "$BIOS_PATH/$TAG" "$SAVES_PATH/$TAG" "$CHEATS_PATH/$TAG"

ASSET_DIR="$BIOS_PATH/$TAG/PPSSPP"
if [ -d "$CORES_PATH/assets" ] && [ ! -f "$ASSET_DIR/ppge_atlas.zim" ]; then
	mkdir -p "$ASSET_DIR"
	cp -a "$CORES_PATH/assets/." "$ASSET_DIR/"
fi

# Per-game compat diff.  Keep an existing file: it is the user's override.
# PPSSPP's GetSysDirectory(DIRECTORY_SYSTEM) is <memstick>/PSP/SYSTEM, and the
# memstick is the frontend save dir.  GetPSPFolderOf() strips the extra "PSP"
# only when the save dir itself is named PSP, so a tag like LIBRETRO_PPSSPP
# needs Saves/<TAG>/PSP/SYSTEM; the older PSP tag needs Saves/PSP/SYSTEM.
case "$TAG" in
	PSP) COMPAT_DIR="$SAVES_PATH/$TAG/SYSTEM" ;;
	*)   COMPAT_DIR="$SAVES_PATH/$TAG/PSP/SYSTEM" ;;
esac
mkdir -p "$COMPAT_DIR"
if [ -f "$CORES_PATH/compat.ini" ] && [ ! -f "$COMPAT_DIR/compat.ini" ]; then
	cp "$CORES_PATH/compat.ini" "$COMPAT_DIR/compat.ini"
fi

HOME="$USERDATA_PATH"
cd "$HOME"
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1
