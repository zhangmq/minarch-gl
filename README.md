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
| Status | experimental; developed against an RGSP, device testing scheduled |
| Distribution | one Tools pak (`Tools/h700/MinarchGL Installer.pak`); it installs the cores into `Emus/h700/` and can restore stock at any time |
| Upstream | pinned overlay on [LoveRetro/NextUI](https://github.com/LoveRetro/NextUI), see `upstream.lock` |

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

## Build

Prerequisites: `docker`, `git`, `curl`, `zip` or `bsdtar`, ~2 GB free.

```sh
make build            # frontend only          -> dist/minarch-gl.elf
make cores            # + installer + cores     -> dist/MinarchGL.Installer.pak.zip
make check            # upstream drift report (no writes)
make diff             # patches of our overrides vs the pinned base (review)
```

The build:
1. extracts NextUI at `BASE_SHA` (cached under `.cache/`),
2. copies it to `build/tree` and overlays `workspace/`,
3. builds `workspace/all/minarch-gl` and `installer` inside
   the pinned toolchain image,
4. optionally builds the three cores and assembles the pak.

The toolchain is pinned by digest in `upstream.lock`; the frontend elf is
reproducible from (base sha, overlay commit, toolchain digest).

## Install

One pak, and it is a *tool* pak — installing it changes nothing by itself:

```
unzip MinarchGL.Installer.pak.zip into Tools/h700/   # -> /Tools/h700/MinarchGL Installer.pak/
```

Then open **Tools → MinarchGL Installer** and choose **Enable MinarchGL**. That
single action backs up the stock frontend, installs `minarch-gl`, and puts the
three core paks into `Emus/h700/`; the same entry turns into **Disable
MinarchGL** and undoes it (the core paks are hidden again, not deleted). Nothing
under `.system` is touched until you press Enable. **Status** shows what is
installed, the pin behind each core pak, and where its ROM folders are.

ROM folders use the tag of the core pak that the tool installs:

```
Roms/Sega Dreamcast (LIBRETRO_FLYCAST)/        Sega Dreamcast   (flycast)
Roms/Sega Naomi (LIBRETRO_FLYCAST)/            Sega Naomi       (flycast, same tag/pak)
Roms/Nintendo 64 (LIBRETRO_MUPEN64PLUS)/       Nintendo 64      (mupen64plus-next)
Roms/Sega Saturn (LIBRETRO_YABASANSHIRO)/      Sega Saturn      (yabasanshiro)
Roms/Sega Titan Video (LIBRETRO_YABASANSHIRO)/ Sega ST-V        (yabasanshiro, same tag/pak)
```

BIOS files go under `/Bios/<TAG>/`, i.e. `Bios/LIBRETRO_FLYCAST/` (both Dreamcast
and Naomi BIOS files live in that one folder), `Bios/LIBRETRO_MUPEN64PLUS/`, and
`Bios/LIBRETRO_YABASANSHIRO/` (`saturn_bios.bin` and/or `stvbios.zip`).

The tags are not the community ones (`DC`, `SS`, `N64`) on purpose: the Pak
Store already ships stand-alone paks with those names, and two paks cannot share
a tag. `LIBRETRO_FLYCAST` also covers Dreamcast and Naomi with a single pak.

## Upstream sync

`overlay.map` lists the 12 files that are forks of upstream files; they are
merged three-way on every bump (`tools/sync.py merge --to <sha>`). Everything
else changed upstream arrives for free through the assembled tree.
