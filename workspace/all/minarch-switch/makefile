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

include ../../$(PLATFORM)/platform/makefile.env
SDL?=SDL

###########################################################

TARGET = minarch-switch

# The menu is NextUI's own framework -- the same menu.cpp/menu.hpp behind the
# Settings tool.  Compiling the base tree's copy keeps the look identical and
# avoids a vendored fork that would silently drift; if upstream ever moves it,
# this build fails loudly instead of quietly looking different.
MENU_DIR = ../settings

INCDIR = -I. -I../common/ -I../../$(PLATFORM)/platform/ -I$(MENU_DIR)

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
CSRC = ../common/utils.c ../common/api.c ../common/config.c ../common/palette.c \
	../common/scaler.c ../common/http.c ../common/ra_auth.c ../common/ra_offline.c \
	../common/ra_sync.c ../common/ra_event_queue.c ../../$(PLATFORM)/platform/platform.c
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
	cd ../../$(PLATFORM)/libmsettings && make
