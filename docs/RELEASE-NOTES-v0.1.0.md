# minarch-gl v0.1.0 — first release

A community GL/hw-render frontend for **NextUI on Anbernic H700** handhelds. It
adds libretro's `RETRO_ENVIRONMENT_SET_HW_RENDER` (GLES) to NextUI's `minarch`, so
cores that render through GLES run inside NextUI's menus, shaders and savestate
system.

This release is a **single tool pak**. Installing it changes nothing; you pick the
frontend inside its UI, and it installs the cores for you:

```
Tools/h700/Minarch GL.pak          # unzip Minarch.GL.pak.zip here
```

## Install

1. Unzip `Minarch.GL.pak.zip` into `Tools/h700/` on the SD card
   (you get `Tools/h700/Minarch GL.pak/`).
2. On the device: **Tools → Minarch GL → Enable minarch-gl**.
3. Put your ROMs in the folders below and launch them from the menu as usual.

Enable does three things, and only when you press it:

* backs up the stock `minarch.elf` and installs minarch-gl behind a small
  dispatch script (removing the pak later is not enough to revert, use Restore);
* installs the three core paks into `Emus/h700/`;
* renames them to `*.pak.disabled` whenever you switch back, so the stock
  frontend never tries to launch a GLES core it cannot drive.

**Restore stock minarch** in the same menu undoes all of it.

## Systems

| ROM folder | System | Core |
|---|---|---|
| `Roms/Sega Dreamcast (LIBRETRO_FLYCAST)/` | Sega Dreamcast | flycast |
| `Roms/Sega Naomi (LIBRETRO_FLYCAST)/` | Sega Naomi | flycast (same pak) |
| `Roms/Nintendo 64 (LIBRETRO_MUPEN64PLUS)/` | Nintendo 64 | mupen64plus-next |
| `Roms/Sega Saturn (LIBRETRO_YABASANSHIRO)/` | Sega Saturn | yabasanshiro |
| `Roms/Sega Titan Video (LIBRETRO_YABASANSHIRO)/` | Sega ST-V | yabasanshiro (same pak) |

BIOS files go in `/Bios/<TAG>/`:

* `Bios/LIBRETRO_FLYCAST/` — Dreamcast and Naomi BIOS files (e.g. `dc_boot.bin`,
  `dc_flash.bin`, `naomi.zip`, `naomi_boot*.bin`);
* `Bios/LIBRETRO_MUPEN64PLUS/` — N64 BIOS/support files, if your setup uses any;
* `Bios/LIBRETRO_YABASANSHIRO/` — `saturn_bios.bin` and/or `stvbios.zip`.

The tags are the core names on purpose: the Pak Store already ships stand-alone
paks called `DC`, `SS` and `N64`, and two paks cannot share a tag. One tag serving
two systems is normal in NextUI (the Saturn family works the same way).

## NextUI updates

A NextUI update replaces `.system/` and therefore removes the switched frontend.
The tool notices (the frontend shows **Needs reinstall**) and re-applies on one
press; meanwhile a boot hook hides the GL-only core paks again, so a stock
frontend is never asked to load a core it cannot run.

## Artifacts

| File | Size (B) | sha256 |
|---|---:|---|
| `minarch-gl.elf` | 667248 | `056fdc29b22b0d3b4947d294896e9baf583e87fbe9774d659e3979cd78f8a66e` |
| `Minarch.GL.pak.zip` | 4850962 | `a12d843b0b5093ba53007054972d5c1f1b4ddc407a7fd158da57ca28532814ba` |

`Minarch.GL.pak.zip` contains the frontend (md5 `51d0fc1799b595b166c69b1eda8f6e32`),
the switcher and the three cores; the switcher's file logic is covered by
`make test` on the build host.

## Build provenance

| Input | Pin |
|---|---|
| NextUI base tree | `LoveRetro/NextUI@5291344b3aceae5fa197437a70824afb41e2d338` (2026-10-20) |
| Toolchain | `ghcr.io/loveretro/h700-toolchain@sha256:bf5d8f9f5442cad789aa77176bc059ec823caefa3258bf30780eee681c777119` |
| flycast | `flyinghead/flycast@aa97a6d64fb47d3ce0febaa575b26d975dd916e4` (v2.0) |
| mupen64plus-next | `libretro/mupen64plus-libretro-nx@6752836de8b224febfd5708444755b77712ac939` |
| yabasanshiro | `zhangmq/yabause@9b36fd6ef8985129d2f829a24d0d3e43f8a621e2` (branch `h700`) |

```sh
make build   # frontend
make cores   # + switcher + 3 cores -> dist/Minarch.GL.pak.zip
make test    # host-side switcher tests
```

## License

The frontend and the switcher are derived from NextUI and distributed under
PolyForm Noncommercial 1.0.0 (`LICENSE`, `NOTICE`) — non-commercial use only. The
bundled cores are GPLv2 and ship as separate `.so` files; their sources are the
pinned repositories above:

* flycast — <https://github.com/flyinghead/flycast/tree/aa97a6d64fb47d3ce0febaa575b26d975dd916e4>
* mupen64plus-next — <https://github.com/libretro/mupen64plus-libretro-nx/tree/6752836de8b224febfd5708444755b77712ac939>
* yabasanshiro — <https://github.com/zhangmq/yabause/tree/9b36fd6ef8985129d2f829a24d0d3e43f8a621e2>

The core build recipes (flags, pins, submodule notes) are in
`workspace/h700/cores/makefile`, so the binaries can be rebuilt from those
sources with the pinned toolchain.

## Status

Experimental. Built and verified by construction (`make test` for the switcher's
file logic, elf identity and executable bits inside the pak); **device smoke
testing on an RGSP was not completed for this build**.
