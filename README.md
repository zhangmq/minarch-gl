# minarch-gl

> [!WARNING]
> This project works by replacing NextUI's `minarch.elf` on your SD card.
> Unexpected bugs are possible, and updates here may lag behind NextUI changes.
> The installer keeps a backup and can restore stock, but please judge the risk
> yourself before installing.

An experimental **GL/hw-render frontend for NextUI on H700 devices**
(RG28XX / RG34XX / RG35XX / RG40XX / RGcubeXX / RGSP …).

Stock NextUI's `minarch` implements libretro's *video output* with OpenGL
(shader presets), but not the *core-side* hw-render API:
`RETRO_ENVIRONMENT_SET_HW_RENDER` is a stub that returns success without
supplying `get_proc_address` / `get_current_framebuffer`, and PAKS.md says so
outright ("does not implement the OpenGL libretro APIs").  As a result, cores
that render through GLES — flycast (Dreamcast/NAOMI), mupen64plus-next (N64),
yabasanshiro (Saturn) and swanstation (PlayStation) — cannot run under stock
minarch, and the community ships them as *standalone* emulators with no NextUI
integration.

minarch-gl adds that missing half: the frontend owns a GLES context, gives the
core a framebuffer, presents the core's texture through the same shader chain,
and drives frame pacing around it.  The result keeps NextUI's menus, per-core
options, savestates, resume and shaders while running the cores above.

| | |
|---|---|
| Platform | `h700` only (the four core pins were chosen for this SoC) |
| Status | experimental; installer + frontend exercised on an RGSP (2026-11-20), SwanStation prototype tested on device; other core smoke tests ongoing |
| Distribution | one small Tools pak (`Tools/h700/MinarchGL Installer.pak`, ~110 KB); the frontend and the cores are downloaded from this project's releases, and the tool can restore stock at any time |
| Upstream | pinned overlay on [LoveRetro/NextUI](https://github.com/LoveRetro/NextUI) (`main`); base SHA and toolchain digest in `upstream.lock` |

## Not official

This is a **community, non-official** project. It is not affiliated with or
endorsed by LoveRetro/NextUI, MinUI, TrimUI or the device vendor. Report issues here, not
upstream. The source is derived from NextUI, which is licensed under PolyForm
Noncommercial 1.0.0 — see `LICENSE` and `NOTICE`. Do not use it commercially.

Development is AI-assisted: changes are written with agent tooling, then
reviewed and tested on device by a human. Every test we rely on is a real
`make`/device run, not a claim.

## What is in here

```
installer/                   # the Tools-pak app (standalone; not an overlay)
gles-cores/<plat>/           # core pins, CMake toolchain, GL headers per SoC (h700 today)
workspace/                   # only files that replace a NextUI file, at their NextUI path
├── all/minarch-gl/          #   the frontend overlay
└── h700/platform/platform.c #   external-pad overlay (until NextUI#881 lands)
paks/MinarchGL/              # the installer pak template
paks/LIBRETRO_*/             # the core pak templates it installs into Emus/h700
paks/pakz/                   # post_install.sh templates for the boot-installed pakz
tools/                       # base/build/diff/sync helpers
overlay.map                  # which repo files override which upstream file
upstream.lock                # pinned NextUI sha + toolchain digest + yaba sha
```

The build assembles NextUI's tree from a pinned tarball and overlays `workspace/`
on top, so everything outside `workspace/` is ours alone and everything else in
the base tree (menus, input, rewind, RetroAchievements, core build machinery) is
inherited unchanged. Design and working notes are kept outside this repository.

## Cores

Emulation comes from four libretro cores, each built from a pinned commit
(`make cores`) and shipped as its own pak:

| pak | core | source | ref | commit |
|---|---|---|---|---|
| `LIBRETRO_FLYCAST` | flycast | [flyinghead/flycast](https://github.com/flyinghead/flycast) | tag `v2.0` | `aa97a6d6` |
| `LIBRETRO_MUPEN64PLUS` | mupen64plus_next | [libretro/mupen64plus-libretro-nx](https://github.com/libretro/mupen64plus-libretro-nx) | `master` | `6752836d` |
| `LIBRETRO_YABASANSHIRO` | yabasanshiro | [zhangmq/yabasanshiro-pirry](https://github.com/zhangmq/yabasanshiro-pirry) | `h700-libretro` | `eb32d3f9` |
| `LIBRETRO_SWANSTATION` | swanstation | [libretro/swanstation](https://github.com/libretro/swanstation) | `main` | `631f0fb3` |

The pins are the only version this project tracks: they live in
`gles-cores/h700/makefile` (the Saturn one is mirrored in `upstream.lock`, which
`tools/sync.py check` compares), and the installer's manifest is generated from
them. Bumping a core is therefore a commit that changes a pin, not a rebuild of
a moving branch.

Why these sources:

- **flycast** — flyinghead's repository, not `libretro/flycast`: that one is a
  deprecated fork without the GLES renderer or the PVR message queue. `v2.0` is
  the build picked for this SoC by the 2026-09 version sweep.
- **mupen64plus-next** — the compatibility leader on H700-class hardware;
  parallel-n64 was rejected because its LLE RDP is too heavy for the SoC. The
  build forces the GLES3 renderer (`FORCE_GLES3=1`), the same hw-render path as
  the other three.
- **yabasanshiro** — our fork of pirrypirrypirry's 1.6.2 base, a single branch
  (`h700-libretro`); its libretro port is CMake and is driven by our own
  toolchain file. This is the only core source we maintain.
- **swanstation** — libretro's hard fork of DuckStation. It has a GLES 3.0+
  renderer, an AArch64 JIT, PGXP and a built-in OpenBIOS (so no BIOS image is
  required); our H700 prototype ran 2D and 3D titles at 59.6 fps at 1x.

`ref` is where the commit is easiest to find, not a branch the build follows:
the build fetches the pin by SHA, so a pin that is older than the branch tip
(and flycast's is, by years) builds exactly the pinned commit. Only
yabasanshiro sets a branch override; the other three clone the default branch
and then check out the SHA on top.

## Build

Prerequisites: `docker`, `git`, `curl`, `zip` or `bsdtar`, ~2 GB free.

```sh
make build            # frontend only           -> dist/minarch-gl.elf
make installer        # + the installer tool    -> dist/minarch-gl-installer.elf
make cores            # + the four cores        -> build/paks/<TAG>.pak/
make release          # everything + packaging  -> all release assets in dist/
make test             # host-side installer tests (no device needed)
make check            # upstream drift report (no writes)
make diff             # patches of our overrides vs the pinned base (review)
```

The build:
1. extracts NextUI at `BASE_SHA` (cached under `.cache/`),
2. copies it to `build/tree` and overlays `workspace/`,
3. builds `workspace/all/minarch-gl` and `installer` inside
   the pinned toolchain image,
4. optionally builds the four cores and stages one flat pak per core.

`make release` then writes, into `dist/`: `core-list.json`, `SHA256SUMS`,
`minarch-gl_h700.elf`, `LIBRETRO_<TAG>_h700.pak.tar.gz` (one per core),
`MinarchGL.Installer.pak.zip` (the store pak), `MinarchGL.Installer.pakz` and
`MinarchGL.Full_h700.pakz` (boot-installable updates, below) and
`MinarchGL.Uninstall.pakz` -- the assets a GitHub release serves, all checksums
generated by the script.

The toolchain is pinned by digest in `upstream.lock`; the frontend elf is
reproducible from (base sha, overlay commit, toolchain digest).

No vendor blob is needed to build: the Saturn core links against the Mali
driver only for the `DT_NEEDED` record.  When `.cache/cores/h700-deps` has no
`libmali.so.0` (a clean checkout, or CI), `tools/build.sh` generates a stub with
the same SONAME and `--no-as-needed` keeps the entry; the device's real driver
resolves the `gl*` symbols at run time.  The same workflow builds a release on
GitHub Actions when a `v*` tag is pushed (`.github/workflows/release.yml`).

## Install

The pak is a *tool* pak and carries **no components**: installing it changes
nothing by itself.

```
unzip MinarchGL.Installer.pak.zip into Tools/h700/   # -> Tools/h700/MinarchGL Installer.pak/
```

Open **Tools → MinarchGL Installer**. With online updates on (the default) the
tool fetches this project's latest manifest and checksums at startup (two small
requests, ~5 s cap); with them off it reads only what is already on the card.
Either way:

- **Setup User Data** (`Y SETUP`): downloads `minarch-gl_h700.elf`, verifies its
  sha256 against the release's `SHA256SUMS`, saves a copy of the stock frontend,
  installs the frontend copy. Nothing under `.system/bin` changes yet.
- **Main page** → `Y ENABLE` copies the installed frontend over
  `.system/bin/minarch.elf`; the same row turns into **Disable MinarchGL**.
  `A CORES` lists the cores and the state of the installed copy; `A` on a core
  row opens its page, where `Y INSTALL` / `Y UPDATE` / `Y UNINSTALL` manage that
  core pak, and its ROM and BIOS folders are listed one by one: `Y CREATE` makes
  the ones that are missing, `Y DELETE` removes one after a confirmation (the
  files inside it are lost). `A STATUS` shows the frontend state, the data
  directory, the hashes of the installed and stock copies, and each core's
  installed and available pin. `Y CLEAN USER DATA` removes MinarchGL's own files
  (frontend copies, the stock backup, the cache and downloads; stock is restored
  first when our build is running) and stops on a page only `B` leaves. It
  deliberately does **not** remove the core paks under `Emus/h700/`, nor ROMs,
  saves or BIOS: the core paks stay installed (and can no longer run once the
  stock frontend is back), so remove `Emus/h700/LIBRETRO_*.pak/` by hand or use
  `MinarchGL.Uninstall.pakz`, which removes everything this project installed.
- `B` is back; on the two root pages it leaves the tool.

What the actions touch (nothing else is ever written):

| action | writes | leaves alone |
|---|---|---|
| `Setup User Data` / `Update MinarchGL` | `.userdata/<plat>/minarch-gl/minarch-gl.elf`, `minarch-stock.elf` (first setup), `core-list.json`, `SHA256SUMS`, `dl/`; the active frontend too when MinarchGL is enabled | `Emus/`, `Roms/`, `Saves/`, `Bios/` |
| `Enable` / `Disable MinarchGL` | `.system/<plat>/bin/minarch.elf` | everything else |
| core `Install` / `Update` / `Uninstall` | `Emus/<plat>/<TAG>.pak/` (only that tag) | ROM folders, BIOS, other paks |
| `Create` / `Delete` folders on a core page | `Roms/<name> (<TAG>)/` and `Bios/<TAG>/` | ROM files elsewhere |
| `Clean User Data` | restores the stock frontend, then removes `.userdata/<plat>/minarch-gl/` (keeping `prefs`) | `Emus/` core paks, `Roms/`, `Saves/`, `Bios/`, the installer pak |
| `MinarchGL.Uninstall.pakz` | removes the state directory, every `LIBRETRO_*` core pak, the stock backup and the installer pak | `Roms/`, `Saves/`, `Bios/`, foreign paks |

Every component comes from the latest release of this repository
(`https://github.com/zhangmq/minarch-gl/releases/latest/download/<asset>`);
`MINARCHGL_BASE_URL` points the tool at another directory (a local HTTP server,
or `file:///...`) for testing, with the same checksum checks. A card with no
network still shows what it has and still enables/disables/uninstalls.

## Updates and offline

Two switches decide how this tool stays current; both are on the **Main page**
(and the online one is also on the setup page, which is all a card without a
frontend has -- an `Uninstall MINARCHGL` drops you back there).

**Online updates** — `Y` toggles it (the row flips between *Disable* and
*Enable*). The choice is kept in `.userdata/<plat>/minarch-gl/prefs`, survives
reinstalls of the pak, and defaults to on. With it off the tool never opens a
network connection: the startup check reads the local copies, and `Install` /
`Update` rows that would need a download are not shown at all. The setup page
says `Online updates are off; enable them to set up.` and, below it,
`No network? Apply MinarchGL.Full_<plat>.pakz.`. `file://` URLs still work --
that is how the Full Pack below installs itself. Uninstalling the frontend keeps
the preference, so the setup page can still turn the network back on.

**Boot-installed updates** — two `.pakz` files, applied by NextUI's own boot
installer instead of by this tool:

| file | what it installs | default online updates |
|---|---|---|
| `MinarchGL.Installer.pakz` | only `Tools/<plat>/MinarchGL Installer.pak/` | on |
| `MinarchGL.Full_h700.pakz` | the installer pak, the frontend and every core pak | off |

Copy one to the card root and restart the device: the boot installer unpacks it
at the card root, runs its `post_install.sh` and deletes it. Both scripts log to
`.userdata/<plat>/logs/` and always return 0, so a failed pack cannot stop the
rest of the boot. The Full Pack carries the same assets a release serves and
installs them through this tool's own CLI (`--install`, `--core-install`), so
checksums, the stock backup, atomic replacement and the enabled/disabled state
are the same as the in-app path.

Both write their default into `prefs` **only when the card has no preference
yet**; a later choice in the app is never overridden. When the in-app online
path is also available (the usual store install), the two channels do not
conflict: whichever file the user applies wins, and the app reports what is
actually installed.

A core is installed as `Emus/h700/LIBRETRO_<TAG>.pak/` -- the same flat shape as
NextUI's own extras paks (`launch.sh`, `default.cfg`, `<core>_libretro.so`) plus
a `version` file holding `<core> <pin>`; that pin is the only version the tool
compares. Cores are only ever added to the release list, never removed, so an
installed pak stays recognisable.

ROM folders use the tag of the core pak that the tool installs:

```
Roms/Sega Dreamcast (LIBRETRO_FLYCAST)/        Sega Dreamcast   (flycast)
Roms/Sega Naomi (LIBRETRO_FLYCAST)/            Sega Naomi       (flycast, same tag/pak)
Roms/Nintendo 64 (LIBRETRO_MUPEN64PLUS)/       Nintendo 64      (mupen64plus-next)
Roms/Sega Saturn (LIBRETRO_YABASANSHIRO)/      Sega Saturn      (yabasanshiro)
Roms/Sega Titan Video (LIBRETRO_YABASANSHIRO)/ Sega ST-V        (yabasanshiro, same tag/pak)
Roms/Sony PlayStation (LIBRETRO_SWANSTATION)/  Sony PlayStation  (swanstation)
```

BIOS files go under `/Bios/<TAG>/`: that is the directory minarch hands to the
core as its libretro system directory, and where every NextUI pak expects them.
NextUI ships no BIOS files, so you bring your own; the installer creates the
empty directory together with the core's ROM folders.

Which files a core needs there, and how it expects them laid out inside it, is
the core's own business -- follow that core's libretro documentation (flycast,
mupen64plus-next, yabasanshiro and swanstation each document their own BIOS).
SwanStation's BIOS is optional: it falls back to the OpenBIOS image built into
the core. This project deliberately keeps no copy of that list, so it cannot go
stale.

The tags are not the community ones (`DC`, `SS`, `N64`, `PS`) on purpose: the
Pak Store already ships stand-alone paks with those names, and two paks cannot
share a tag. `LIBRETRO_FLYCAST` also covers Dreamcast and Naomi with a single
pak; `LIBRETRO_SWANSTATION` owns the PS1 folder, so existing
`Roms/Sony PlayStation (PS)` trees need renaming to
`Roms/Sony PlayStation (LIBRETRO_SWANSTATION)` (the installer creates the new
directory when you press CREATE).

## Upstream sync

`overlay.map` lists the 15 files that are forks of upstream files; they are
merged three-way on every bump (`tools/sync.py merge --to <sha>`). Everything
else changed upstream arrives for free through the assembled tree.
