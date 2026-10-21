#!/bin/sh
# Fetch (and cache) the pinned NextUI base tree.
#
# Prints the tree path on stdout; progress goes to stderr, so this can be used
# in $(...).  The tree is a plain extraction of GitHub's tarball at BASE_SHA --
# no git history, no submodules -- which is all the build needs.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/upstream.lock"

CACHE=${CACHE_DIR:-$ROOT/.cache}
DEST="$CACHE/base/$BASE_SHA"

if [ -f "$DEST/workspace/all/minarch/minarch.c" ]; then
	printf '%s\n' "$DEST"
	exit 0
fi

SHA=${1:-$BASE_SHA}
if [ "$SHA" != "$BASE_SHA" ]; then
	DEST="$CACHE/base/$SHA"
	if [ -f "$DEST/workspace/all/minarch/minarch.c" ]; then
		printf '%s\n' "$DEST"
		exit 0
	fi
fi

TAR="$CACHE/NextUI-$SHA.tar.gz"
mkdir -p "$CACHE/base"
if [ ! -f "$TAR" ]; then
	echo "base.sh: fetching $BASE_REPO@$SHA" >&2
	curl -fsSL "https://codeload.github.com/$BASE_REPO/tar.gz/$SHA" -o "$TAR.part"
	mv "$TAR.part" "$TAR"
fi

rm -rf "$DEST"
mkdir -p "$DEST"
tar -xzf "$TAR" -C "$DEST" --strip-components=1
echo "base.sh: extracted $DEST" >&2
printf '%s\n' "$DEST"
