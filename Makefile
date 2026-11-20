# minarch-gl -- build entry points (thin wrappers over tools/*.sh)

.PHONY: base build installer cores release check test diff clean distclean

all: build

base:
	sh tools/base.sh

# Frontend only: the daily path (~15 s with a warm dependency cache).
build:
	sh tools/build.sh

# Frontend + installer (+ the lite installer pak).  ~20 s: use this to try the
# installer UI on the device without rebuilding the cores.
installer:
	BUILD_INSTALLER=1 sh tools/build.sh

# Frontend + installer + the four cores, staged under build/paks/.
cores:
	BUILD_CORES=1 sh tools/build.sh

# Everything above, then package the release assets into dist/.
release:
	BUILD_CORES=1 sh tools/build.sh
	sh tools/release.sh

# Upstream drift report (three-way merge trial, read-only).
check:
	python3 tools/sync.py check

# Host-side tests for the part of the installer that touches the SD card.
test:
	sh tools/test.sh

# dist/patches/*.patch, for review only (not an update mechanism).
diff:
	sh tools/diff.sh

clean:
	rm -rf build dist

distclean: clean
	rm -rf .cache
