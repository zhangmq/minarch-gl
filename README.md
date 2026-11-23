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
that render through GLES — flycast and flycastrophe (Dreamcast/NAOMI),
mupen64plus-next (N64), yabasanshiro (Saturn), swanstation (PlayStation) and
ppsspp (PlayStation Portable) — cannot run under stock minarch, and the
community ships them as *standalone* emulators with no NextUI integration.

minarch-gl adds that missing half: the frontend owns a GLES context, gives the
core a framebuffer, presents the core's texture through the same shader chain,
and drives frame pacing around it.  The result keeps NextUI's menus, per-core
options, savestates, resume and shaders while running the cores above.

| | |
|---|---|
| Platform | `h700` only (the six core pins were chosen for this SoC) |
| Status | experimental; installer + frontend exercised on an RGSP (2026-11-20/23), with Flycastrophe (Naomi Crazy Taxi 32 → 55 fps), SwanStation, PPSSPP (GoW CoO 30 fps) and the other cores tested on device. v0.1.7 added `Update Core List` and moved Flycastrophe to 0.3.0; v0.1.8 added `Update Installer` (the tool replaces itself inside its pak, no reboot) and dropped the store pak — the `.pakz` is the only installer package; v0.1.9 (2026-11-23) moved yabasanshiro, ppsspp and mupen64plus-next to their tips |
| Distribution | one small Tools pak (`Tools/h700/MinarchGL Installer.pak`, ~290 KB); the frontend and the cores are downloaded from this project's releases, and the tool can restore stock at any time |
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

Emulation comes from six libretro cores, each built from a pinned commit
(`make cores`) and shipped as its own pak:

| pak | core | source | ref | commit |
|---|---|---|---|---|
| `LIBRETRO_FLYCAST` | flycast | [flyinghead/flycast](https://github.com/flyinghead/flycast) | tag `v2.0` | `aa97a6d6` |
| `LIBRETRO_MUPEN64PLUS` | mupen64plus_next | [libretro/mupen64plus-libretro-nx](https://github.com/libretro/mupen64plus-libretro-nx) | `master` | `15bada85` |
| `LIBRETRO_YABASANSHIRO` | yabasanshiro | [zhangmq/yabasanshiro-pirry](https://github.com/zhangmq/yabasanshiro-pirry) | `h700-libretro` | `f86b66ea` |
| `LIBRETRO_SWANSTATION` | swanstation | [libretro/swanstation](https://github.com/libretro/swanstation) | `main` | `631f0fb3` |
| `LIBRETRO_PPSSPP` | ppsspp | [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp) | `master` | `a2f0b60d` |
| `LIBRETRO_FLYCASTROPHE` | flycastrophe | [Helaas/Flycastrophe](https://github.com/Helaas/Flycastrophe) | `main` | `8981ec5` |

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
  required). On H700 the **software renderer** is the better default: at 1x the
  GL path adds no image quality (it exists for resolution upscaling and PGXP)
  while paying for GPU readbacks and driver overhead, and titles that dipped
  under OpenGL hold 59.6 fps in software. If you do keep OpenGL, turn off PGXP
  and `Use Software Renderer For Readbacks` first — both are CPU-heavy and the
  readback path renders every primitive twice.
- **ppsspp** — upstream hrydgard/ppsspp; its libretro port lives in-tree under
  `libretro/` and builds with the GLES 2/3 renderer. The gcc 8.3/aarch64 build fixes
  live in `patches/ppsspp.patch`. The pak ships the pinned, **unmodified master
  assets** (fonts, flash0, ppge_atlas, lang, compat.ini): GoW's title/menu needs
  them, and using a different PPSSPP version's assets is unsupported.
- **flycastrophe** — upstream [Helaas/Flycastrophe](https://github.com/Helaas/Flycastrophe),
  a fork that follows `flyinghead/flycast` master by merge and ports Deadcast's
  speed work: the AICA on its own thread, a faster SH4 recompiler, one draw per
  run of one-triangle modifier volumes, `glDrawRangeElements` for indexed draws,
  a code cache on disk and frame skipping when a game falls behind. It is still
  in development upstream (GPLv2). gcc 8.3 needs one source patch,
  `patches/flycastrophe.patch`, plus `-lstdc++fs` at link time.

`ref` is where the commit is easiest to find, not a branch the build follows:
the build fetches the pin by SHA, so a pin that is older than the branch tip
(and flycast's is, by years) builds exactly the pinned commit. Only
yabasanshiro sets a branch override; the other five clone the default branch
and then check out the SHA on top.

Flycastrophe is a second Dreamcast/NAOMI core and the **fast** option of the
two: it is the same emulator with a set of speed changes ported from Deadcast
(a trimmed Flycast for a handheld), it trades accuracy for speed, and its core
options default to the fast path. It is still under development upstream, so
prefer it when a game needs the frames and keep flycast for whatever it
regresses. It gets its own pak, its own ROM folders (`Roms/Sega Dreamcast
(LIBRETRO_FLYCASTROPHE)`, `Roms/Sega Naomi (LIBRETRO_FLYCASTROPHE)`) and its
own save states; the two can be installed side by side and are picked per
folder. On the H700 it took Naomi Crazy Taxi from 32 to 55 fps, while titles
that already held 60 fps were unchanged. The pak defaults `HLE BIOS` to on:
with a real `dc_boot.bin` this core stops on the Dreamcast menu for discs whose
CDI boot sector is defective, where flycast v2.0 booted them directly; Naomi
has no BIOS boot and ignores it.

## Build

Prerequisites: `docker`, `git`, `curl`, `zip` or `bsdtar`, ~2 GB free.

```sh
make build            # frontend only           -> dist/minarch-gl.elf
make installer        # + the installer tool    -> dist/minarch-gl-installer.elf
make cores            # + the six cores         -> build/paks/<TAG>.pak/
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
4. optionally builds the six cores and stages one flat pak per core.

`make release` then writes, into `dist/`: `core-list.json`, `SHA256SUMS`,
`minarch-gl_h700.elf`, `LIBRETRO_<TAG>_h700.pak.tar.gz` (one per core),
`MinarchGL.Installer.pakz` and `MinarchGL.Full_h700.pakz` (boot-installable
updates, below) and `MinarchGL.Uninstall.pakz` -- the assets a GitHub release
serves, all checksums generated by the script.  There is no store zip: the tool
is not listed in the Pak Store, and the `.pakz` doubles as its self-update.

The toolchain is pinned by digest in `upstream.lock`; the frontend elf is
reproducible from (base sha, overlay commit, toolchain digest).

No vendor blob is needed to build: the Saturn core links against the Mali
driver only for the `DT_NEEDED` record, and PPSSPP is linked with unresolved
GL/EGL symbols that the device's `libmali` resolves at run time.  When `.cache/cores/h700-deps` has no
`libmali.so.0` (a clean checkout, or CI), `tools/build.sh` generates a stub with
the same SONAME and `--no-as-needed` keeps the entry; the device's real driver
resolves the `gl*` symbols at run time.  The same workflow builds a release on
GitHub Actions when a `v*` tag is pushed (`.github/workflows/release.yml`).

## Install

The pak is a *tool* pak and carries **no components**: installing it changes
nothing by itself.  It is installed from `MinarchGL.Installer.pakz`: copy that
file to the card root and restart; NextUI's boot installer unpacks it to
`Tools/h700/MinarchGL Installer.pak/` and deletes the archive.  The same file is
what `Update Installer` inside the tool downloads, so there is exactly one
installer package to look for.

Open **Tools → MinarchGL Installer**. The first screen is drawn from what is
already on the card: the startup never touches the network. With online updates
on (the default) every action that needs this project's release — setup, update,
a core install, or `Y UPDATE CORE LIST` — fetches the latest manifest and
checksums first (two small requests, ~5 s cap each) and works from that copy;
with them off only what is already on the card is used. Either way:

- **Setup User Data** (`Y SETUP`): downloads `minarch-gl_h700.elf`, verifies its
  sha256 against the release's `SHA256SUMS`, saves a copy of the stock frontend,
  installs the frontend copy. Nothing under `.system/bin` changes yet.
- **Main page** → `Y ENABLE` copies the installed frontend over
  `.system/bin/minarch.elf`; the same row turns into **Disable MinarchGL**.
  `A CORES` lists the cores and the state of the installed copy; `A` on a core
  row opens its page, where `Y INSTALL` / `Y UPDATE` / `Y UNINSTALL` manage that
  core pak, and its ROM and BIOS folders are listed one by one: `Y CREATE` makes
  the ones that are missing, `Y DELETE` removes one after a confirmation (the
  files inside it are lost). `Y UPDATE CORE LIST` refetches the release manifest
  and reports what it holds — the number of cores, any tag the previous list did
  not have, and how many installed cores that list can update — without
  installing anything; it is the only way to see a core that a release added,
  since nothing is fetched at startup. `Y UPDATE INSTALLER` replaces the tool's
  own files from the release's `MinarchGL.Installer.pakz`, inside the pak
  directory (the new version starts on the next launch -- the running binary
  cannot be written to). `A STATUS` shows the frontend state, the
  data directory, the hashes of the installed and stock copies, and each core's
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
| `Update Core List` | `.userdata/<plat>/minarch-gl/core-list.json`, `SHA256SUMS`, `dl/` | `Emus/`, `Roms/`, `Saves/`, `Bios/` |
| `Update Installer` | the pak's own files (`Tools/<plat>/MinarchGL Installer.pak/`), `dl/MinarchGL.Installer.pakz` | everything outside the pak |
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
network connection: every action reads only the local copies, `Update Core List`
is not offered, and `Install` / `Update` rows that would need a download are not
shown at all. The setup page
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
yet**; a later choice in the app is never overridden. The in-app online path and
a `.pakz` applied at boot do not conflict: whichever file the user applies wins,
and the app reports what is actually installed.

A core is installed as `Emus/h700/LIBRETRO_<TAG>.pak/` -- the same flat shape as
NextUI's own extras paks (`launch.sh`, `default.cfg`, `<core>_libretro.so`) plus
a `version` file holding `<core> <pin>`; that pin is the only version the tool
compares. Cores are only ever added to the release list, never removed, so an
installed pak stays recognisable.

ROM folders use the tag of the core pak that the tool installs:

```
Roms/Sega Dreamcast (LIBRETRO_FLYCAST)/            Sega Dreamcast    (flycast)
Roms/Sega Dreamcast (LIBRETRO_FLYCASTROPHE)/       Sega Dreamcast    (flycastrophe, the faster one)
Roms/Sega Naomi (LIBRETRO_FLYCAST)/                Sega Naomi        (flycast, same tag/pak)
Roms/Sega Naomi (LIBRETRO_FLYCASTROPHE)/           Sega Naomi        (flycastrophe, same tag/pak)
Roms/Nintendo 64 (LIBRETRO_MUPEN64PLUS)/           Nintendo 64       (mupen64plus-next)
Roms/Sega Saturn (LIBRETRO_YABASANSHIRO)/          Sega Saturn       (yabasanshiro)
Roms/Sega Titan Video (LIBRETRO_YABASANSHIRO)/     Sega ST-V         (yabasanshiro, same tag/pak)
Roms/Sony PlayStation (LIBRETRO_SWANSTATION)/      Sony PlayStation  (swanstation)
Roms/Sony PlayStation Portable (LIBRETRO_PPSSPP)/  Sony PlayStation Portable (ppsspp)
```

Two paks own Dreamcast and NAOMI on purpose. **flycast** (`v2.0`) is the safe
pick — upstream's compatibility core. **If you care about performance, prefer
`flycastrophe`**: on the H700 it takes Naomi Crazy Taxi from 32 to 55 fps and
leaves titles that already held 60 fps alone. It is still **under development
upstream**, though, so treat it as the fast option rather than the fallback:
keep flycast installed and move a game back to its folder when flycastrophe
regresses it.

BIOS files go under `/Bios/<TAG>/`: that is the directory minarch hands to the
core as its libretro system directory, and where every NextUI pak expects them.
NextUI ships no BIOS files, so you bring your own; the installer creates the
empty directory together with the core's ROM folders.

Which files a core needs there, and how it expects them laid out inside it, is
the core's own business -- follow that core's libretro documentation (flycast,
mupen64plus-next, yabasanshiro, swanstation and ppsspp each document their own
BIOS).
SwanStation's BIOS is optional: it falls back to the OpenBIOS image built into
the core. This project deliberately keeps no copy of that list, so it cannot go
stale.

PPSSPP is the one core with an asset tree rather than a BIOS: it expects the
upstream `assets/` contents under `Bios/<TAG>/PPSSPP/` (fonts, flash0,
`ppge_atlas.zim`, `lang/`, `compat.ini`).  The `LIBRETRO_PPSSPP` pak ships the
pinned, unmodified master assets and its `launch.sh` seeds that directory on
first run.  Do not mix assets from a different PPSSPP build.  On H700 the pak
also installs a small per-game compat diff (two sections, four settings) at
`Saves/<TAG>/PSP/SYSTEM/compat.ini` (note the extra `PSP/`; this is PPSSPP's
`memstick/PSP/SYSTEM`, not `Saves/<TAG>/SYSTEM`) that enables the 30 fps hack
and disables the 60 fps hack for God of War: Chains of Olympus.  The shipped
assets stay factory-default; edit that override to change the choice.
`ppsspp_frame_duplication` defaults off to match standalone PPSSPP.

The tags are not the community ones (`DC`, `SS`, `N64`, `PS`) on purpose:
stand-alone paks with those names exist, and two paks cannot share a tag. `LIBRETRO_FLYCAST` also covers Dreamcast and Naomi with a single
pak; `LIBRETRO_SWANSTATION` owns the PS1 folder, so existing
`Roms/Sony PlayStation (PS)` trees need renaming to
`Roms/Sony PlayStation (LIBRETRO_SWANSTATION)` (the installer creates the new
directory when you press CREATE).  `LIBRETRO_PPSSPP` likewise owns
`Roms/Sony PlayStation Portable (LIBRETRO_PPSSPP)`; the existing standalone
`Emus/h700/PSP.pak` keeps using `Roms/PlayStation Portable (PSP)`, so testing
both means copying the ROMs (or renaming the folder and giving the standalone
its own copy).  Only the suffix matters to the menu: a folder is matched by its
`(<TAG>)`, so a card that already has the older
`Roms/PlayStation Portable (LIBRETRO_PPSSPP)` keeps working unrenamed.

## Upstream sync

`overlay.map` lists the 16 files that are forks of upstream files; they are
merged three-way on every bump (`tools/sync.py merge --to <sha>`). Everything
else changed upstream arrives for free through the assembled tree.
