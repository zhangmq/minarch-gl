# The frontend switcher

`Tools/h700/Minarch GL.pak` is the only supported way to run minarch-gl. It is a
TOOL pak: dropping it on the card changes nothing until **Enable minarch-gl** is
pressed inside the UI.

## Why it exists

Every NextUI pak — the stock extras and ours — invokes the bare name
`minarch.elf`, and the launcher resolves that through
`PATH=$SYSTEM_PATH/bin:...` (`skeleton/SYSTEM/h700/paks/MinUI.pak/launch.sh`).
That leaves exactly one place to switch the frontend. Emulator paks cannot do it
without either bundling a second frontend per system or silently overwriting the
system one; neither is acceptable, so the decision is an explicit user action.

## What "enabled" means on disk

```
$SYSTEM_PATH/bin/minarch.elf          # a small script when enabled (marker MINARCH-GL-DISPATCH)
$SYSTEM_PATH/bin/minarch-gl.elf       # copied from the pak's payload/
$SYSTEM_PATH/bin/minarch-stock.elf    # the stock binary, moved aside on install
```

The script execs `minarch-gl.elf`, and falls back to `minarch-stock.elf` if the
GL binary disappears. A half-broken install must never leave the device without
a frontend.

Install and restore are atomic per file: everything is written to `*.new` and
`rename(2)`d into place, so an interrupted write cannot truncate the frontend.
`sync()` runs after a successful change.

## Core paks

The three cores ride inside the same pak (`payload/<TAG>.pak/`) and are copied to
`Emus/h700/<TAG>.pak/` on enable, so installing the frontend and installing the
cores cannot drift apart.

| Tag | Systems | Core |
|---|---|---|
| `LIBRETRO_FLYCAST` | Dreamcast **and** Naomi (one tag, one pak) | flycast |
| `LIBRETRO_MUPEN64PLUS` | Nintendo 64 | mupen64plus-next |
| `LIBRETRO_YABASANSHIRO` | Sega Saturn **and** Sega Titan Video (ST-V) | yabasanshiro |

The tags deliberately avoid the community/store names (`DC`, `SS`, `N64`):
two paks cannot share a tag, and the store already ships stand-alone paks with
those names.

Each core pak carries a `minarch-gl-only` marker. When the stock frontend is
active the switcher renames them to `<TAG>.pak.disabled`; NextUI's `hide()`
(`workspace/all/common/utils.c`) skips that suffix and `hasEmu()` only accepts
`<TAG>.pak`, so the systems disappear from the menu instead of launching a core
the stock frontend cannot drive. Switching back to GL renames them to `.pak`
again.

## NextUI updates

A NextUI update replaces `.system/` wholesale, so the dispatch script and both
binaries vanish while `.userdata/h700/minarch-gl/state` still says `gl`.

* The switcher detects this (`state == gl` but `minarch.elf` is the stock ELF)
  and shows **Reinstall minarch-gl**.
* While GL is marked enabled, a boot hook
  (`.userdata/h700/.hooks/boot.d/minarch-gl-guard.sync.sh`) also hides the GL-only
  paks again, so a stock frontend never gets asked to load a GLES core in the
  window before the user re-runs the tool.

## Undo by hand

```
$SYSTEM_PATH/bin/minarch.elf          delete the script
$SYSTEM_PATH/bin/minarch-stock.elf    rename it back to minarch.elf
$SYSTEM_PATH/bin/minarch-gl.elf       delete
Emus/h700/LIBRETRO_*.pak              rename to *.pak.disabled (or delete)
.userdata/h700/.hooks/boot.d/minarch-gl-guard.sync.sh   delete
```

The tool does exactly this in **Restore stock minarch**.

## UI

`workspace/all/minarch-switch/` is a normal NextUI tool app: it compiles the base
tree's `workspace/all/settings/menu.cpp` (the framework behind Settings) so the
menus, fonts, pills and button hints are the system ones, not a lookalike. The
only coupling to upstream is that file; if upstream moves it the build fails
loudly instead of quietly rendering something else.
