###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
	$(error please specify PLATFORM, eg. PLATFORM=h700 make)
endif

ifeq (,$(CROSS_COMPILE))
	$(error missing CROSS_COMPILE for this toolchain)
endif

###########################################################

include ../$(PLATFORM)/platform/makefile.env
SDL?=SDL

###########################################################

TARGET = minarch-gl-installer

# This tool draws its own rows and its own title pill, so it needs the common
# drawing API (api.c: pills, hints, fonts, colours) but not the menu framework
# (settings/menu.cpp).  Reusing the framework's look without its item callbacks
# keeps the screens simple: the framework quits on B and confirms on A, while
# this tool navigates with A, acts with Y and leaves with B.
INCDIR = -I. -I../all/common/ -I../$(PLATFORM)/platform/

CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++

CFLAGS += $(OPT)
CFLAGS += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\"
CXXFLAGS += $(CFLAGS) -std=c++17 -Wall -Wextra
# -lcrypto: sha256 of the three frontend copies (installer-core.hpp).  The same
# library NextUI's own tools already link for RetroAchievements.
LDFLAGS += -lmsettings -lcrypto

PRODUCT = build/$(PLATFORM)/$(TARGET).elf

# C translation units stay C (they are shared with minarch); g++ only compiles
# the C++ file and links.  Source list mirrors the Settings tool minus its
# bluetooth agent and its menu, neither of which is used here.
CSRC = ../all/common/utils.c ../all/common/api.c ../all/common/config.c ../all/common/palette.c \
	../all/common/scaler.c ../all/common/http.c ../all/common/ra_auth.c ../all/common/ra_offline.c \
	../all/common/ra_sync.c ../all/common/ra_event_queue.c ../$(PLATFORM)/platform/platform.c
COBJ = $(addprefix build/$(PLATFORM)/,$(notdir $(CSRC:.c=.o)))

all: $(PREFIX_LOCAL)/include/msettings.h
	mkdir -p build/$(PLATFORM)
	$(CC) $(CSRC) -c $(CFLAGS) $(LDFLAGS)
	mv $(notdir $(CSRC:.c=.o)) build/$(PLATFORM)/
	$(CXX) $(TARGET).cpp $(COBJ) -o $(PRODUCT) $(CXXFLAGS) $(LDFLAGS) -lstdc++

clean:
	rm -f $(PRODUCT)
	rm -rf build

$(PREFIX_LOCAL)/include/msettings.h:
	cd ../$(PLATFORM)/libmsettings && make
