# Upstream sync policy

This repository is an overlay on a pinned NextUI tree (`upstream.lock`). The goal
is that a base bump is a **mechanical, reviewable, repeatable** operation — not
a merge-by-memory exercise.

## What arrives for free, what needs work

| Upstream change | What happens |
|---|---|
| A file we do **not** override (menus, input, cheats, rewind, RetroAchievements, core build machinery, platform files other than `platform.c`) | Inherited automatically by the assembled tree. Only compile errors or behavioural regressions need attention. |
| A file listed in `overlay.map` (12 files) | We carry a fork of it. Upstream edits must be merged into our copy three-way. |
| A new upstream source file (upstream adds a translation unit) | The overlay build passes `SOURCE=` on the command line (see `workspace/all/minarch-gl/makefile`). New upstream files are **not** picked up automatically — check the drift report and update the wrapper's `UPSTREAM_SOURCE`. |
| Upstream renames/moves a file we override | Treated as a breaking bump: update `overlay.map`, the wrapper's `UPSTREAM_SOURCE`, and the overlay file name in one commit. |
| Upstream changes `.gitignore`-level conventions or the license | Follow it (currently PolyForm Noncommercial 1.0.0). |

## Bump checklist

```sh
tools/sync.py check                 # drift report, no writes
tools/sync.py merge --to <sha>      # three-way merge into workspace/... (conflict markers)
# resolve conflicts; check the commits listed in the report are actually absorbed
tools/build.sh                      # frontend must compile
BUILD_CORES=1 tools/build.sh        # cores + paks (only when core pins or APIs changed)
# device smoke: boot each pak, check fps/present telemetry against the baseline
tools/sync.py pin <sha>             # writes upstream.lock
git add -A && git commit -m "chore(upstream): bump to <sha>"
```

Rules of thumb:

* **Do not "rebase" the overlay.** It is not a git fork of upstream; it is a set
  of files that happen to share paths. `git merge` of upstream would always be
  clean and always lose upstream fixes silently.
* A merge is only done when the merged file **compiles and runs**. If the
  upstream change is large and semantic, prefer re-porting our intent onto the
  new upstream file over blindly accepting a textual merge.
* Keep the diff visible: `tools/diff.sh` writes per-file patches to
  `dist/patches/` for review. They are generated artifacts — never edit them.
* The toolchain digest is part of the pin. If the image tag resolves to a new
  digest, that is a separate bump with its own md5 baseline.

## Known traps

1. **`SOURCE` is hardcoded** in `workspace/all/minarch-gl/makefile`. It exists to
   drop `../common/scaler.c`, redirect `api.c`/`platform.c` to our copies, and
   append our new files. If upstream adds/removes a translation unit, the build
   will not notice by itself — read the drift report, or compare upstream's
   `workspace/all/minarch/makefile` `SOURCE` against our wrapper.
2. **Upstream C++ migration.** NextUI has an open PR (#793) converting the C
   sources to C++20. If it lands, the overlay (17.6k lines of C) must be ported
   before bumping past it. Keeping the overlay C-subset-clean makes that cheaper.
3. **Upstream shader/frame-time work** (#796 shader sets, #848 frame-time
   callback) touches the same presentation/pacing area as our overlay. Merge
   these knowingly, and re-run the fps baseline.
4. **Upstream hw-render.** If `RETRO_ENVIRONMENT_SET_HW_RENDER` is implemented
   upstream (#627), most of `ma_core.c`/`ma_environment.c`/`ma_gl.c` becomes
   redundant. Re-evaluate the split instead of maintaining two implementations.
5. **Core pins are ours.** `gles-cores/h700/makefile` is not a fork of
   upstream's file; upstream adding cores does not affect us. The one shared
   dependency is the core build template (`workspace/all/cores/makefile`), which
   comes from the base tree.
6. **yabause pin duplication.** `YABAUSE_SHA` in `upstream.lock` and
   `yabasanshiro_HASH` in the cores makefile must agree; `tools/sync.py pin`
   warns when they diverge.
7. **The installer compiles an upstream file.** `installer/`
   links `../settings/menu.cpp` (and its `menu.hpp`) on purpose: that is the
   system menu framework, and vendoring a copy would drift. If upstream moves or
   renames it, the build fails loudly — update `MENU_DIR` in
   `installer/makefile` (and re-check the API the tool uses:
   `MenuList`, `MenuItem`, `MenuItemType::List`, `showOverlay`).
8. **The installer writes `.system` at runtime.** Not a sync concern, but a bump
   that changes the frontend's binary name or the pak path scheme would invalidate
   the dispatch script; `docs/INSTALLER.md` is the contract, and `make test` covers
   the rest.

## Cadence

* `tools/sync.py check` — any time, read-only, seconds.
* `merge` + build + device smoke — monthly, or before a release.
* Crash/audio/security fixes — immediately, as a small targeted bump.
* Deferring a bump is allowed; deferring a `check` report is not (that is how
  upstream fixes get silently lost).
