#!/bin/sh
# MinarchGL Installer -- manages the MinarchGL frontend and its core paks.
#
# Writes under $SYSTEM_PATH/bin only when the user asks for it (enable, update,
# uninstall).  Logs go to $LOGS_PATH: the pak directory is treated as read-only
# at runtime (Pak Store updates overwrite it and uninstall deletes it).
cd "$(dirname "$0")" || exit 1
./minarch-gl-installer.elf > "$LOGS_PATH/minarch-gl-installer.txt" 2>&1
