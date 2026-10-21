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
| Status | experimental, actively tested on an RGSP |
| Distribution | self-contained paks (`Emus/h700/<TAG>.pak`) — stock `.system` stays untouched |
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
workspace/                  # the slice: paths identical to NextUI's tree
├── all/minarch-gl/         #   frontend overlay (27 files; 12 of them replace upstream files)
└── h700/cores/makefile     #   the three cores we build (flycast, mupen64plus_next, yabasanshiro)
paks/{DC,NAOMI,N64,SATURN}/ #   pak templates: launch.sh, pak.json, default.cfg
tools/                      #   base/build/diff/sync helpers
overlay.map                 #   which repo files override which upstream file
upstream.lock               #   pinned NextUI sha + toolchain digest + yabause sha
docs/UPSTREAM-SYNC.md       #   how upstream changes are merged
```

Nothing in `workspace/` outside `all/minarch-gl/` is copied upstream code: the
build assembles NextUI's tree from a pinned tarball and overlays `workspace/` on
top. Everything else in the base tree (menus, input, rewind, RetroAchievements,
core build machinery) is inherited unchanged.

## Build

Prerequisites: `docker`, `git`, `curl`, `zip`, ~2 GB free.

```sh
make build            # frontend only          -> dist/minarch-gl.elf
make cores            # + 3 cores, 4 paks     -> dist/*.pak.zip
make check            # upstream drift report (no writes)
make diff             # patches of our overrides vs the pinned base (review)
```

The build:
1. extracts NextUI at `BASE_SHA` (cached under `.cache/`),
2. copies it to `build/tree` and overlays `workspace/`,
3. builds `workspace/all/minarch-gl` inside the pinned toolchain image,
4. optionally builds the three cores and assembles the pak zips.

The toolchain is pinned by digest in `upstream.lock`; the frontend elf is
reproducible from (base sha, overlay commit, toolchain digest).

## Install

Copy a pak zip to the SD card, unzip it, delete the zip:

```
/Emus/h700/SATURN.pak/     Sega Saturn   (yabasanshiro)
/Emus/h700/N64.pak/        Nintendo 64   (mupen64plus-next)
/Emus/h700/DC.pak/         Dreamcast     (flycast)
/Emus/h700/NAOMI.pak/      Sega NAOMI    (flycast)
```

Each pak carries its own `minarch-gl.elf` and core, so it never replaces
`$SYSTEM_PATH/bin/minarch.elf`; removing the folder is a complete rollback.
ROM folders use the matching tag: `Roms/Sega Saturn (SATURN)/`, `Roms/Nintendo 64 (N64)/`,
`Roms/Sega Dreamcast (DC)/`, `Roms/Sega NAOMI (NAOMI)/`. BIOS files go under
`/BIOS/<TAG>/` (see NextUI's emulator notes for which files each system needs).

The `DC` and `N64` tags are also used by stand-alone community paks on the Pak
Store — install one or the other for those systems.

## Upstream sync

`overlay.map` lists the 12 files that are forks of upstream files; they are
merged three-way on every bump (`tools/sync.py merge --to <sha>`). Everything
else changed upstream arrives for free through the assembled tree. The policy,
the bump checklist and the known traps are in
[docs/UPSTREAM-SYNC.md](docs/UPSTREAM-SYNC.md).
