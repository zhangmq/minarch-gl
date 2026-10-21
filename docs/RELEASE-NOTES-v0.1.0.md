# minarch-gl v0.1.0 — first release

A community GL/hw-render frontend overlay for **NextUI on Anbernic H700**
handhelds. It adds libretro's `RETRO_ENVIRONMENT_SET_HW_RENDER` (GLES) to
NextUI's `minarch`, so cores that render through GLES can run inside NextUI's
menus, shaders and savestate system.

This release ships four **self-contained paks**. Each pak carries its own
`minarch-gl.elf` and core, so nothing under `.system/bin/` is replaced: stock
NextUI keeps running everything else, and deleting the pak folder is a complete
rollback.

| Pak | System | Core |
|---|---|---|
| `DC.pak` | Sega Dreamcast | flycast |
| `NAOMI.pak` | Sega NAOMI | flycast |
| `N64.pak` | Nintendo 64 | mupen64plus-next |
| `SATURN.pak` | Sega Saturn | yabasanshiro |

> Not affiliated with or endorsed by LoveRetro/NextUI, MinUI, TrimUI or
> Anbernic. Experimental software: back up your SD card.

## Install

1. Copy a pak zip to the SD card, e.g. `Emus/h700/`.
2. Unzip it and delete the zip; you get `Emus/h700/<TAG>.pak/`.
3. Put ROMs in `Roms/<System> (<TAG>)/` and BIOS files in `/BIOS/<TAG>/`
   (see NextUI's notes for the files each system needs).

`DC` and `N64` are also used by stand-alone community paks on the Pak Store —
install one or the other for those systems. Remove the `.pak` folder to roll back.

## Artifacts

| File | Size (B) | sha256 |
|---|---:|---|
| `minarch-gl.elf` | 667248 | `056fdc29b22b0d3b4947d294896e9baf583e87fbe9774d659e3979cd78f8a66e` |
| `DC.pak.zip` | 2258640 | `7f427926cdfa80dd8d5f64fa14b80c3f5b58b7a7f926158ad897b6f89ebacc9f` |
| `NAOMI.pak.zip` | 2258700 | `587fcb510c4b5852ce408be87636d9f62296041b7b8ccb28d86184a9b43157da` |
| `N64.pak.zip` | 2008615 | `3e8b7608beef03a51f080b6d0aeaf642e61bc4a11ad35ddca3710fea45abec1c` |
| `SATURN.pak.zip` | 1159295 | `87c05a673bec10b501b7a04f21216c0ec18909a638650500142bcb24f16d7988` |

Each pak contains `launch.sh`, `pak.json`, `default.cfg`, `minarch-gl.elf`, the
core `.so`, plus `LICENSE` / `NOTICE` / `LICENSE-<core>`.

## Build provenance

| Input | Pin |
|---|---|
| NextUI base tree | `LoveRetro/NextUI@5291344b3aceae5fa197437a70824afb41e2d338` (2026-10-20) |
| Toolchain | `ghcr.io/loveretro/h700-toolchain@sha256:bf5d8f9f5442cad789aa77176bc059ec823caefa3258bf30780eee681c777119` |
| flycast | `flyinghead/flycast@aa97a6d64fb47d3ce0febaa575b26d975dd916e4` (v2.0) |
| mupen64plus-next | `libretro/mupen64plus-libretro-nx@6752836de8b224febfd5708444755b77712ac939` |
| yabasanshiro | `zhangmq/yabause@9b36fd6ef8985129d2f829a24d0d3e43f8a621e2` (branch `h700`) |

`minarch-gl.elf` md5 is `51d0fc1799b595b166c69b1eda8f6e32`; it was reproduced
from an empty `.cache/` and `build/` on 2026-10-21. The cores' md5s are recorded
in `HANDOFF.md`. Build:

```sh
make build   # frontend
make cores   # + 3 cores + 4 pak zips
```

## License

The frontend is derived from NextUI and is distributed under PolyForm
Noncommercial 1.0.0 (`LICENSE`, `NOTICE`) — non-commercial use only. The bundled
cores are GPLv2 and are shipped as separate `.so` files; their sources are the
pinned repositories above:

* flycast — <https://github.com/flyinghead/flycast/tree/aa97a6d64fb47d3ce0febaa575b26d975dd916e4>
* mupen64plus-next — <https://github.com/libretro/mupen64plus-libretro-nx/tree/6752836de8b224febfd5708444755b77712ac939>
* yabasanshiro — <https://github.com/zhangmq/yabause/tree/9b36fd6ef8985129d2f829a24d0d3e43f8a621e2>

The core build recipes (flags, pins, submodule notes) are in
`workspace/h700/cores/makefile` in this repository, so the binaries above can be
rebuilt from those sources with the pinned toolchain.

## Status

Experimental. Built and verified structurally (elf identity inside each pak, zip
layout, executable `launch.sh`, aarch64 dynamic dependencies); **device smoke
testing on an RGSP was not completed for this build** — see `HANDOFF.md` for the
fps baseline and the pending checklist.
