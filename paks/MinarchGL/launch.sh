#!/bin/sh
# Minarch GL -- frontend switcher and the only supported way to install it.
#
# The user picks Enable in the UI; only then does anything under
# $SYSTEM_PATH/bin change.  The cores are not shipped as separate paks: this pak
# carries them and installs the LIBRETRO_* paks the first time GL is enabled, so
# "install the frontend" and "install the cores" cannot drift apart.
cd "$(dirname "$0")" || exit 1
./minarch-switch.elf > ./switch.log 2>&1
