# HANDOFF — switching to this repository as a DSH workspace

This repo was bootstrapped from the NextUI workspace (2026-10-21). The DSH
session that created it had `/home/zmq/projects/NextUI` as its file sandbox,
which is why everything was staged in `workspace/tmp/minarch-gl-repo` first and
then moved here. From now on, work on minarch-gl in a DSH session **whose
workspace is this directory**.

## New session

```
workspace: /home/zmq/projects/minarch-gl
```

DSH will inject this repo's `AGENTS.md` (untracked, see `.gitignore`) and the
file sandbox will be this tree — no per-command approvals for normal edits,
builds under `.cache/`/`build/`/`dist/` stay inside the workspace.

## First checks

```sh
pwd                                   # /home/zmq/projects/minarch-gl
git log --oneline -3                  # bootstrap commit
docker image inspect ghcr.io/loveretro/h700-toolchain@sha256:bf5d8f9f5442cad789aa77176bc059ec823caefa3258bf30780eee681c777119 >/dev/null && echo toolchain ok
make build                            # frontend only; first run downloads ~40 MB base tarball
```

Expected: `dist/minarch-gl.elf`.

The bootstrap build against base `5291344b` produced
`51d0fc1799b595b166c69b1eda8f6e32` (667 KB, aarch64, NEEDED: SDL2, GLESv2,
SDL2_image, SDL2_ttf, ...). It has **not** been run on the device yet. The elf
deployed from the old NextUI-workspace tree was built from 2026-09-07-era
sources, so its md5 differs — compare behaviour, not bytes.

## Then

1. `make diff` and read the 12 patches — this is the reviewable definition of
   "what we changed" (use it to sanity-check the overlay after the base bump).
2. `make cores` — builds flycast / mupen64plus_next / yabasanshiro and the four
   pak zips. Core clones live in `.cache/cores/`; first run needs network.
3. Device smoke via the helpers in the NextUI workspace (see AGENTS.md), then
   `tools/sync.py pin` is NOT needed yet — the pin is already at the bootstrap
   base; only bump after a verified build.
4. Create the GitHub repo (`zhangmq/minarch-gl`), push, and cut the first
   release with `dist/*.pak.zip` + `dist/minarch-gl.elf` + checksums.

## What was NOT done here

- No GitHub remote was created or pushed.
- The NextUI fork still contains `workspace/all/minarch-gl/` and the extra core
  entries; removing them (phase 4 of the migration plan) is a separate decision
  and should leave a tag for history.
- Device smoke of the freshly built elf/paks was not run in the bootstrap
  session; treat the first build as unverified until it boots on the RGSP.
