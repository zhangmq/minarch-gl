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

# The menu is NextUI's own framework -- the same menu.cpp/menu.hpp behind the
# Settings tool.  Compiling the base tree's copy keeps the look identical and
# avoids a vendored fork that would silently drift; if upstream ever moves it,
# this build fails loudly instead of quietly looking different.
MENU_DIR = ../all/settings

INCDIR = -I. -I../all/common/ -I../$(PLATFORM)/platform/ -I$(MENU_DIR)

CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++

CFLAGS += $(OPT)
CFLAGS += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\"
CXXFLAGS += $(CFLAGS) -std=c++17
LDFLAGS += -lmsettings -lcrypto

PRODUCT = build/$(PLATFORM)/$(TARGET).elf

# C translation units stay C (they are shared with minarch); g++ only links and
# compiles the two C++ files.  Source list mirrors the Settings tool minus its
# bluetooth agent, which is not used here.
CSRC = ../all/common/utils.c ../all/common/api.c ../all/common/config.c ../all/common/palette.c \
	../all/common/scaler.c ../all/common/http.c ../all/common/ra_auth.c ../all/common/ra_offline.c \
	../all/common/ra_sync.c ../all/common/ra_event_queue.c ../$(PLATFORM)/platform/platform.c
COBJ = $(addprefix build/$(PLATFORM)/,$(notdir $(CSRC:.c=.o)))

all: $(PREFIX_LOCAL)/include/msettings.h
	mkdir -p build/$(PLATFORM)
	$(CC) $(CSRC) -c $(CFLAGS) $(LDFLAGS)
	mv $(notdir $(CSRC:.c=.o)) build/$(PLATFORM)/
	$(CXX) $(TARGET).cpp $(MENU_DIR)/menu.cpp $(COBJ) -o $(PRODUCT) $(CXXFLAGS) $(LDFLAGS) -lstdc++

clean:
	rm -f $(PRODUCT)
	rm -rf build

$(PREFIX_LOCAL)/include/msettings.h:
	cd ../$(PLATFORM)/libmsettings && make
