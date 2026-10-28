# minarch-gl -- GL hw-render/shader-chain frontend, kept out of the upstream
# minarch target.
#
# Design (see workspace/tmp/minarch-gl-rewrite/PLAN.md):
#   * upstream all/minarch/ stays byte-identical; nothing here edits it.
#   * this directory holds ONLY files that must differ from upstream.  With no
#     source files in it, the target is exactly upstream minarch built through
#     this wrapper -- that is the step-1 baseline the rewrite starts from.
#   * the build merges upstream minarch/ into MERGED_DIR, copies this
#     directory's files over it, and builds there, so relative paths inside the
#     merged tree (../common, ../../$(PLATFORM)/..., ../../hash.txt) resolve as
#     they do in minarch/ itself.
ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif
ifeq (,$(PLATFORM))
$(error please specify PLATFORM, eg. PLATFORM=h700 make)
endif
ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

MERGED_DIR = ../.minarch-gl-build
UPSTREAM    = ../minarch
TARGET      = minarch-gl

# Upstream's SOURCE list, verbatim from all/minarch/makefile.  The wrapper hands
# it to the recursive make on the COMMAND LINE, which is how the overlay swaps
# an upstream translation unit for one of ours without editing upstream or
# shipping a makefile into the merged tree: replace the path here (api.c,
# platform.c, ...) and drop the corresponding file into this directory.
# generic_video.c needs no entry -- h700 platform.c #includes it, so overriding
# platform.c is what selects our copy.
UPSTREAM_SOURCE = minarch.c ma_cheats.c ma_rewind.c ma_audio.c ma_input.c \
	ma_options.c ma_frontend_opts.c ma_saves.c ma_video.c ma_core.c ma_game.c \
	ma_environment.c ma_config.c ma_menu.c ma_runframe.c \
	../common/utils.c ../common/config.c api.c \
	../common/notification.c platform.c \
	../common/http.c ../common/ra_badges.c ../common/ra_offline.c \
	../common/ra_sync.c ../common/ra_event_queue.c ra_integration.c chd_reader.c \
	glslp.c ma_chain.c ma_gl.c ma_present.c ma_pace.c ma_preset.c ma_perf.c

all:
	rm -rf $(MERGED_DIR)
	mkdir -p $(MERGED_DIR)
	tar -C $(UPSTREAM) --exclude=./build --exclude=.git -cf - . | tar -C $(MERGED_DIR) -xf -
	@# Overlay sources only.  The merged tree keeps UPSTREAM's makefile so the
	@# recursive make below builds upstream minarch; copying this wrapper in
	@# would make it recurse into itself.  (When the overlay eventually needs a
	@# different SOURCE list, that is done with the MERGED=1 switch this file
	@# used to carry, not by shipping this file into the tree.)
	@files="$$(ls *.c *.h 2>/dev/null)"; [ -n "$$files" ] && cp -f $$files $(MERGED_DIR)/ || true
	@# Pre-fetched frontend deps (libretro-common, libchdr).  The upstream
	@# makefile clones them into this tree; tools/build.sh seeds a cache here so
	@# a normal build does not re-fetch them (they are unpinned upstreams, so a
	@# re-fetch would also silently change the binary -- DEPS_REFRESH=1 rebuilds
	@# the cache deliberately).
	@if [ -d ../.minarch-gl-deps ]; then cp -a ../.minarch-gl-deps/. $(MERGED_DIR)/; fi
	@# Those clone rules also graft the shipped makefile wrappers onto the dep
	@# (libchdr.makefile -> libchdr/makefile); skipping the clone skips that, and
	@# then `cd libchdr && make build install` has no build target.
	@if [ -f $(MERGED_DIR)/libchdr.makefile ]; then cp -f $(MERGED_DIR)/libchdr.makefile $(MERGED_DIR)/libchdr/makefile; fi
	@# rcheevos: the shipped makefile clones the pinned repo into rcheevos/src on
	@# first use, and the rcheevos/makefile rule wipes rcheevos/ to place itself.
	@# Seeding both files first makes that rule a no-op (it has no prerequisites)
	@# and its clone check (`src/include/rc_client.h` exists) skip the fetch.
	@if [ -d ../.minarch-gl-deps/rcheevos-src ]; then \
		mkdir -p $(MERGED_DIR)/rcheevos; \
		cp -f $(MERGED_DIR)/rcheevos.makefile $(MERGED_DIR)/rcheevos/makefile; \
		cp -a ../.minarch-gl-deps/rcheevos-src $(MERGED_DIR)/rcheevos/src; \
	fi
	$(MAKE) -C $(MERGED_DIR) -f makefile PLATFORM=$(PLATFORM) UNION_PLATFORM=$(UNION_PLATFORM) \
		SOURCE="$(UPSTREAM_SOURCE)"
	mkdir -p build/$(PLATFORM)
	cp -f $(MERGED_DIR)/build/$(PLATFORM)/minarch.elf build/$(PLATFORM)/$(TARGET).elf
	rm -rf $(MERGED_DIR)

clean:
	rm -rf build $(MERGED_DIR)

.PHONY: all clean
