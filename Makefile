# minarch-gl -- build entry points (thin wrappers over tools/*.sh)

.PHONY: base build cores paks all check test diff clean distclean

all: build cores paks

base:
	sh tools/base.sh

build:
	sh tools/build.sh

cores:
	BUILD_CORES=1 sh tools/build.sh

paks: cores

check:
	python3 tools/sync.py check

# Host-side tests for the part of the switcher that touches the SD card.
test:
	sh tools/test.sh

diff:
	sh tools/diff.sh

clean:
	rm -rf build dist

distclean: clean
	rm -rf .cache
