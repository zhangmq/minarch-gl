# minarch-gl

An experimental **GL/hw-render frontend for NextUI on Anbernic H700 devices**
(RG28XX / RG34XX / RG35XX / RG40XX / RGcubeXX / RGSP …).

Stock NextUI's `minarch` implements libretro's *video output* with OpenGL
(shader presets), but not the *core-side* hw-render API:
`RETRO_ENVIRONMENT_SET_HW_RENDER` is a stub that returns success without
supplying `get_proc_address` / `get_current_framebuffer`, and PAKS.md says so
outright ("does not implement the OpenGL libretro APIs").  As a result, cores
that render through GLES — flycast (Dreamcast/NAOMI), mupen64plus-next (N64),
yabasanshiro (Saturn) — cannot run under stock minarch, and the community ships
them as *standalone* emulators with no NextUI integration.

minarch-gl adds that missing half: the frontend owns a GLES context, gives the
core a framebuffer, presents the core's texture through the same shader chain,
and drives frame pacing around it.  The result keeps NextUI's menus, per-core
options, savestates, resume and shaders while running the cores above.

| | |
|---|---|
| Platform | `h700` only (the three core pins were chosen for this SoC) |
| Status | experimental; installer + frontend exercised on an RGSP (2026-11-19), game smoke tests pending |
| Distribution | one small Tools pak (`Tools/h700/MinarchGL Installer.pak`, ~110 KB); the frontend and the cores are downloaded from this project's releases, and the tool can restore stock at any time |
| Upstream | pinned overlay on [LoveRetro/NextUI](https://github.com/LoveRetro/NextUI) (`main`); base SHA and toolchain digest in `upstream.lock` |

## Not official

This is a **community, non-official** project. It is not affiliated with or
endorsed by LoveRetro/NextUI, MinUI, TrimUI or Anbernic. Report issues here, not
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
tools/                       # base/build/diff/sync helpers
overlay.map                  # which repo files override which upstream file
upstream.lock                # pinned NextUI sha + toolchain digest + yaba sha
```

The build assembles NextUI's tree from a pinned tarball and overlays `workspace/`
on top, so everything outside `workspace/` is ours alone and everything else in
the base tree (menus, input, rewind, RetroAchievements, core build machinery) is
inherited unchanged. Design and working notes are kept outside this repository.

## Cores

Emulation comes from three libretro cores, each built from a pinned commit
(`make cores`) and shipped as its own pak:

| pak | core | source | ref | commit |
|---|---|---|---|---|
| `LIBRETRO_FLYCAST` | flycast | [flyinghead/flycast](https://github.com/flyinghead/flycast) | tag `v2.0` | `aa97a6d6` |
| `LIBRETRO_MUPEN64PLUS` | mupen64plus_next | [libretro/mupen64plus-libretro-nx](https://github.com/libretro/mupen64plus-libretro-nx) | `master` | `6752836d` |
| `LIBRETRO_YABASANSHIRO` | yabasanshiro | [zhangmq/yabasanshiro-pirry](https://github.com/zhangmq/yabasanshiro-pirry) | `h700-libretro` | `eb32d3f9` |

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
  the other two.
- **yabasanshiro** — our fork of pirrypirrypirry's 1.6.2 base, a single branch
  (`h700-libretro`); its libretro port is CMake and is driven by our own
  toolchain file. This is the only core source we maintain.

`ref` is where the commit is easiest to find, not a branch the build follows:
the build fetches the pin by SHA, so a pin that is older than the branch tip
(and flycast's is, by years) builds exactly the pinned commit. Only
yabasanshiro sets a branch override; the other two clone the default branch and
then check out the SHA on top.

## Build

Prerequisites: `docker`, `git`, `curl`, `zip` or `bsdtar`, ~2 GB free.

```sh
make build            # frontend only           -> dist/minarch-gl.elf
make installer        # + the installer tool    -> dist/minarch-gl-installer.elf
make cores            # + the three cores       -> build/paks/<TAG>.pak/
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
4. optionally builds the three cores and stages one flat pak per core.

`make release` then writes, into `dist/`: `core-list.json`, `SHA256SUMS`,
`minarch-gl_h700.elf`, `LIBRETRO_<TAG>_h700.pak.tar.gz` (one per core),
`MinarchGL.Installer.pak.zip` and `MinarchGL.Uninstall.pakz` -- the assets a
GitHub release serves, all checksums generated by the script.

The toolchain is pinned by digest in `upstream.lock`; the frontend elf is
reproducible from (base sha, overlay commit, toolchain digest).

## Install

The pak is a *tool* pak and carries **no components**: installing it changes
nothing by itself.

```
unzip MinarchGL.Installer.pak.zip into Tools/h700/   # -> Tools/h700/MinarchGL Installer.pak/
```

Open **Tools → MinarchGL Installer**. The tool checks this project's latest
release (one HTTP request, ~5 s cap), then:

- **Needs setup** → `Y INSTALL`: downloads `minarch-gl_h700.elf`, verifies its
  sha256 against the release's `SHA256SUMS`, saves a copy of the stock frontend,
  installs the frontend copy. Nothing under `.system/bin` changes yet.
- **Main page** → `Y ENABLE` copies the installed frontend over
  `.system/bin/minarch.elf`; the same row turns into **Disable MinarchGL**.
  `A STATUS` shows what is on the card, the frontend's hash, and one row per
  core; `A` on a core row opens its page, where `Y INSTALL` / `Y UPDATE` /
  `Y UNINSTALL` manage that core pak and `Y CREATE` makes its default ROM
  folder. `Y UNINSTALL MINARCHGL` removes the frontend (restoring stock first)
  and stops on a page only `B` leaves.
- `B` is back; on the two root pages it leaves the tool.

Every component comes from the latest release of this repository
(`https://github.com/zhangmq/minarch-gl/releases/latest/download/<asset>`);
`MINARCHGL_BASE_URL` points the tool at another directory (a local HTTP server,
or `file:///...`) for testing, with the same checksum checks. A card with no
network still shows what it has and still enables/disables/uninstalls.

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
```

BIOS files go under `/Bios/<TAG>/`: that is the directory minarch hands to the
core as its libretro system directory, and where every NextUI pak expects them.
NextUI ships no BIOS files, so you bring your own; the installer creates the
empty directory together with the core's ROM folders.

Which files a core needs there, and how it expects them laid out inside it, is
the core's own business -- follow that core's libretro documentation (flycast,
mupen64plus-next and yabasanshiro each document their own BIOS). This project
deliberately keeps no copy of that list, so it cannot go stale.

The tags are not the community ones (`DC`, `SS`, `N64`) on purpose: the Pak
Store already ships stand-alone paks with those names, and two paks cannot share
a tag. `LIBRETRO_FLYCAST` also covers Dreamcast and Naomi with a single pak.

## Upstream sync

`overlay.map` lists the 15 files that are forks of upstream files; they are
merged three-way on every bump (`tools/sync.py merge --to <sha>`). Everything
else changed upstream arrives for free through the assembled tree.
