#!/bin/sh
# MinarchGL Installer -- installs, updates and removes the MinarchGL frontend and
# its core paks.  The pak itself carries no components: everything it installs
# comes from this project's releases.
#
# The pak directory is read-only at runtime (a Pak Store update overwrites it and
# an uninstall deletes it), so the tool keeps its state under
# .userdata/<platform>/minarch-gl/ and logs to $LOGS_PATH.  Arguments are passed
# through: `minarch-gl-installer.elf --status` works from a shell, and the
# installer's own CLI is what the device smoke tests use.
cd "$(dirname "$0")" || exit 1
./minarch-gl-installer.elf "$@" > "$LOGS_PATH/minarch-gl-installer.txt" 2>&1
