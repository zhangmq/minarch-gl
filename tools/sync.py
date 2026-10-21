#!/usr/bin/env python3
"""Upstream sync helper for minarch-gl.

The repository is an overlay on a pinned NextUI tree.  Upstream changes reach us
in two ways:

  * files we do NOT override -- inherited automatically by the assembled build
    tree, no work needed;
  * files we DO override (overlay.map) -- these are forks of upstream files, so
    every upstream change has to be merged into our copy three-way.

This tool implements that second half:

  tools/sync.py check [--to REF]   report drift, no writes (dry-run merge)
  tools/sync.py merge [--to REF]   write merged copies into the working tree
  tools/sync.py pin REF            record REF in upstream.lock after a green build

REF may be a SHA, a branch name (main), or origin/main.
"""

import argparse
import datetime
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(ROOT, "upstream.lock")
MAP = os.path.join(ROOT, "overlay.map")


def load_lock():
    vals = {}
    with open(LOCK) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, val = line.split("=", 1)
            vals[key.strip()] = val.strip()
    return vals


def load_map():
    pairs = []
    with open(MAP) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) >= 2:
                pairs.append((parts[0], parts[1]))
    return pairs


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def resolve_ref(ref):
    """Resolve a branch name to a SHA without cloning."""
    if re.fullmatch(r"[0-9a-f]{7,40}", ref):
        return ref
    branch = ref.split("/")[-1]
    out = run(["git", "ls-remote", "https://github.com/LoveRetro/NextUI.git",
               f"refs/heads/{branch}"])
    if out.returncode != 0:
        sys.exit(f"error: cannot resolve {ref}: {out.stderr.strip()}")
    return out.stdout.split()[0]


def base_tree(sha):
    out = subprocess.run(["sh", os.path.join(ROOT, "tools", "base.sh"), sha],
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"error: base.sh failed:\n{out.stderr}")
    return out.stdout.strip()


def upstream_commits(repo, base, target):
    """Commit subjects touching workspace/all/minarch or common, via gh (optional)."""
    out = run(["gh", "api", f"repos/{repo}/compare/{base}...{target}"])
    if out.returncode != 0:
        return []
    import json
    try:
        data = json.loads(out.stdout)
    except Exception:
        return []
    prefix = ("workspace/all/minarch/", "workspace/all/common/",
              "workspace/h700/platform/")
    rows = []
    for c in data.get("commits", []):
        files = [f["filename"] for f in (c.get("files") or [])]
        touch = [f for f in files if f.startswith(prefix)]
        if touch:
            rows.append((c["sha"][:7], c["commit"]["message"].splitlines()[0],
                         touch))
    return rows


def merge3(ours, base, theirs):
    """Return (merged_text, conflicts)."""
    with tempfile.TemporaryDirectory() as td:
        op = os.path.join(td, "ours")
        bp = os.path.join(td, "base")
        tp = os.path.join(td, "theirs")
        for path, text in ((op, ours), (bp, base), (tp, theirs)):
            with open(path, "w") as fh:
                fh.write(text)
        out = subprocess.run(
            ["git", "merge-file", "-p", "--diff3",
             "-L", "ours", "-L", "base", "-L", "upstream", op, bp, tp],
            capture_output=True, text=True)
        if out.returncode < 0 or out.returncode == 255:
            sys.exit(f"error: git merge-file failed: {out.stderr.strip()}")
        return out.stdout, out.returncode


def read(path):
    try:
        with open(path) as fh:
            return fh.read()
    except FileNotFoundError:
        return ""


def cmd_check(args, write=False):
    lock = load_lock()
    target = resolve_ref(args.to or lock["BASE_REF"])
    print(f"base {lock['BASE_SHA'][:7]} ({lock['BASE_DATE'][:10]}) -> {target[:7]}")

    base = base_tree(lock["BASE_SHA"])
    tgt = base_tree(target)

    changed, conflicts = [], 0
    for rpath, bpath in load_map():
        bfile, tfile, ofile = (os.path.join(base, bpath),
                               os.path.join(tgt, bpath),
                               os.path.join(ROOT, rpath))
        if not os.path.isfile(tfile):
            print(f"  {rpath}: upstream file missing at target (renamed/removed?)")
            changed.append(rpath)
            continue
        btext, ttext, otext = read(bfile), read(tfile), read(ofile)
        if btext == ttext:
            continue
        _, nconf = merge3(otext, btext, ttext)
        conflicts += nconf
        changed.append(rpath)
        print(f"  {rpath:<45} upstream changed; 3-way merge: "
              f"{'clean' if nconf == 0 else str(nconf) + ' conflict(s)'}")
        if write:
            merged, _ = merge3(otext, btext, ttext)
            with open(ofile, "w") as fh:
                fh.write(merged)

    commits = upstream_commits(lock["BASE_REPO"], lock["BASE_SHA"], target)
    if commits:
        print("\nupstream commits touching our paths:")
        for sha, subject, files in commits:
            print(f"  {sha} {subject[:70]}")
            for f in files[:6]:
                print(f"         {f}")

    if not changed:
        print("\nno changes to overridden files; bump the pin and rebuild.")
    elif write:
        print(f"\nwrote merged copies for {len(changed)} file(s), "
              f"{conflicts} conflict(s) to resolve by hand.")
    else:
        print(f"\n{len(changed)} overridden file(s) need merging, "
              f"{conflicts} conflict(s). Run: tools/sync.py merge --to {target[:7]}")
    return 0 if not conflicts else 1


def cmd_pin(args):
    sha = resolve_ref(args.ref)
    base_tree(sha)
    lock = load_lock()
    text = open(LOCK).read()
    text = re.sub(r"^BASE_SHA=.*$", f"BASE_SHA={sha}", text, flags=re.M)
    text = re.sub(r"^BASE_REF=.*$", f"BASE_REF={args.ref}", text, flags=re.M)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    text = re.sub(r"^BASE_DATE=.*$", f"BASE_DATE={stamp}", text, flags=re.M)
    open(LOCK, "w").write(text)
    print(f"pinned {lock['BASE_REF']} -> {sha[:7]} in upstream.lock")
    print("next: tools/build.sh && (device smoke) && git commit")

    # the yabause pin lives in two places; keep them honest
    mk = os.path.join(ROOT, "workspace/h700/cores/makefile")
    ym = re.search(r"^yabasanshiro_HASH\s*=\s*(\S+)", open(mk).read(), re.M)
    if ym and ym.group(1) != load_lock().get("YABAUSE_SHA"):
        print(f"warning: yabasanshiro_HASH ({ym.group(1)[:7]}) != "
              f"YABAUSE_SHA ({load_lock().get('YABAUSE_SHA', '?')[:7]})")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, fn in (("check", lambda a: cmd_check(a)), ("merge", lambda a: cmd_check(a, True))):
        p = sub.add_parser(name)
        p.add_argument("--to", default=None, help="target ref (default: BASE_REF upstream)")
        p.set_defaults(func=fn)
    p = sub.add_parser("pin")
    p.add_argument("ref")
    p.set_defaults(func=cmd_pin)
    args = ap.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
