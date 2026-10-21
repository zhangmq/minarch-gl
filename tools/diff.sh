#!/bin/sh
# Generate unified diffs of every overridden upstream file (this repo vs the
# pinned base).  The output is a review/release artifact, not a source of truth:
# overlay files stay the only editable copy.
#
#   tools/diff.sh [outdir]   # default: dist/patches
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/upstream.lock"

OUT=${1:-$ROOT/dist/patches}
BASE=$(sh "$ROOT/tools/base.sh")

rm -rf "$OUT"
mkdir -p "$OUT"

n=0
while read -r rpath bpath; do
	case "$rpath" in ''|'#'*) continue ;; esac
	name=$(basename "$rpath").patch
	if [ ! -f "$BASE/$bpath" ]; then
		echo "warn: base is missing $bpath" >&2
		continue
	fi
	diff -u --label "a/$bpath" --label "b/$rpath" \
		"$BASE/$bpath" "$ROOT/$rpath" > "$OUT/$name" || true
	if [ -s "$OUT/$name" ]; then
		n=$((n + 1))
		printf '%s\n' "$name"
	fi
done < "$ROOT/overlay.map"

echo "wrote $n patch(es) to $OUT" >&2
