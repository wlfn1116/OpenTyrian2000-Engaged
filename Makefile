# Build settings

ifneq ($(filter Msys Cygwin, $(shell uname -o)), )
    PLATFORM := WIN32
    TYRIAN_DIR = C:\\TYRIAN
else
    PLATFORM := UNIX
    TYRIAN_DIR = $(gamesdir)/opentyrian2000
endif

WITH_NETWORK := true

# GNU Make conventions: https://www.gnu.org/prep/standards/html_node/Makefile-Conventions.html

SHELL = /bin/sh

CC ?= gcc
INSTALL ?= install
PKG_CONFIG ?= pkg-config

VCS_IDREV ?= git rev-parse --short HEAD

INSTALL_PROGRAM ?= $(INSTALL)
INSTALL_DATA ?= $(INSTALL) -m 644

prefix ?= /usr/local
exec_prefix ?= $(prefix)

bindir ?= $(exec_prefix)/bin
datarootdir ?= $(prefix)/share
datadir ?= $(datarootdir)
docdir ?= $(datarootdir)/doc/opentyrian2000
mandir ?= $(datarootdir)/man
man6dir ?= $(mandir)/man6
man6ext ?= .6
desktopdir ?= $(datarootdir)/applications
icondir ?= $(datarootdir)/icons

# Filesystem hierarchy: https://www.pathname.com/fhs/pub/fhs-2.3.html

gamesdir ?= $(datadir)/games

TARGET := opentyrian2000
SAN_TARGET := opentyrian2000-sanitize

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=obj/%.o)
DEPS := $(SRCS:src/%.c=obj/%.d)
SAN_OBJS := $(SRCS:src/%.c=obj/sanitize/%.o)
SAN_DEPS := $(SRCS:src/%.c=obj/sanitize/%.d)

PYTHON ?= python3
TEST_DATA ?= data
TEST_FIXTURES ?= testing/fixtures/endless
SANITIZER_FLAGS := -O1 -g3 -fno-omit-frame-pointer -fno-sanitize-recover=all \
                   -fsanitize=address,undefined

ifeq ($(WITH_NETWORK), true)
    EXTRA_CPPFLAGS += -DWITH_NETWORK
endif

# Rebuild opentyr.o so the title screen always gets the current short commit id.
OPENTYRIAN_COMMIT := $(shell $(VCS_IDREV) 2>/dev/null && \
                             touch src/opentyrian_version.h)
ifneq ($(OPENTYRIAN_COMMIT), )
    EXTRA_CPPFLAGS += -DOPENTYRIAN_COMMIT='"$(OPENTYRIAN_COMMIT)"'
endif

CPPFLAGS ?= -MMD
CPPFLAGS += -DNDEBUG
CFLAGS ?= -pedantic \
          -Wall \
          -Wextra \
          -Wno-format-truncation \
          -Wno-missing-field-initializers \
          -O2
LDFLAGS ?=
LDLIBS ?=

ifeq ($(WITH_NETWORK), true)
    SDL_CPPFLAGS := $(shell $(PKG_CONFIG) sdl2 SDL2_net --cflags)
    SDL_LDFLAGS := $(shell $(PKG_CONFIG) sdl2 SDL2_net --libs-only-L --libs-only-other)
    SDL_LDLIBS := $(shell $(PKG_CONFIG) sdl2 SDL2_net --libs-only-l)
    # network.c calls getsockopt/getsockname/WSAIoctl directly to turn off SIO_UDP_CONNRESET.
    ifeq ($(PLATFORM), WIN32)
        SDL_LDLIBS += -lws2_32
    endif
else
    SDL_CPPFLAGS := $(shell $(PKG_CONFIG) sdl2 --cflags)
    SDL_LDFLAGS := $(shell $(PKG_CONFIG) sdl2 --libs-only-L --libs-only-other)
    SDL_LDLIBS := $(shell $(PKG_CONFIG) sdl2 --libs-only-l)
endif

ALL_CPPFLAGS = -DTARGET_$(PLATFORM) \
               -DTYRIAN_DIR='"$(TYRIAN_DIR)"' \
               $(EXTRA_CPPFLAGS) \
               $(SDL_CPPFLAGS) \
               $(CPPFLAGS)
# Required engine assumptions: signed char and unfused floats for netplay parity.
ALL_CFLAGS = -std=iso9899:1999 \
             -fsigned-char \
             -ffp-contract=off \
             $(CFLAGS)
ALL_LDFLAGS = $(SDL_LDFLAGS) \
              $(LDFLAGS)
ALL_LDLIBS = -lm \
             $(SDL_LDLIBS) \
             $(LDLIBS)

.PHONY : all
all : $(TARGET)

.PHONY : debug
debug : CPPFLAGS += -UNDEBUG
debug : CFLAGS += -Werror
debug : CFLAGS += -O0
debug : CFLAGS += -g3
debug : all

.PHONY : test test-unit test-replay test-network
test : test-unit test-replay test-network

test-unit : $(TARGET)
	$(PYTHON) testing/run_unit_suite.py --exe ./$(TARGET) --data "$(TEST_DATA)" \
		--fixtures "$(TEST_FIXTURES)"

test-replay : $(TARGET)
	$(PYTHON) testing/run_replay_fixtures.py --exe ./$(TARGET) --data "$(TEST_DATA)"

test-network : $(TARGET)
	$(PYTHON) testing/network_fault_test.py --exe ./$(TARGET) --data "$(TEST_DATA)"

.PHONY : sanitizer sanitize-test
sanitizer : $(SAN_TARGET)

sanitize-test : $(SAN_TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		$(PYTHON) testing/run_unit_suite.py --exe ./$(SAN_TARGET) --data "$(TEST_DATA)" \
		--fixtures "$(TEST_FIXTURES)"
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		$(PYTHON) testing/run_replay_fixtures.py --exe ./$(SAN_TARGET) --data "$(TEST_DATA)"
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		$(PYTHON) testing/network_fault_test.py --exe ./$(SAN_TARGET) --data "$(TEST_DATA)" --rounds 24

.PHONY : installdirs
installdirs :
	mkdir -p $(DESTDIR)$(bindir)
	mkdir -p $(DESTDIR)$(docdir)
	mkdir -p $(DESTDIR)$(man6dir)
	mkdir -p $(DESTDIR)$(desktopdir)
	mkdir -p $(DESTDIR)$(icondir)/hicolor/22x22/apps
	mkdir -p $(DESTDIR)$(icondir)/hicolor/24x24/apps
	mkdir -p $(DESTDIR)$(icondir)/hicolor/32x32/apps
	mkdir -p $(DESTDIR)$(icondir)/hicolor/48x48/apps
	mkdir -p $(DESTDIR)$(icondir)/hicolor/128x128/apps

.PHONY : install
install : $(TARGET) installdirs
	$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(bindir)/
	$(INSTALL_DATA) NEWS README $(DESTDIR)$(docdir)/
	$(INSTALL_DATA) linux/man/opentyrian2000.6 $(DESTDIR)$(man6dir)/opentyrian2000$(man6ext)
	$(INSTALL_DATA) linux/opentyrian2000.desktop $(DESTDIR)$(desktopdir)/
	$(INSTALL_DATA) linux/icons/tyrian2000-22.png $(DESTDIR)$(icondir)/hicolor/22x22/apps/opentyrian2000.png
	$(INSTALL_DATA) linux/icons/tyrian2000-24.png $(DESTDIR)$(icondir)/hicolor/24x24/apps/opentyrian2000.png
	$(INSTALL_DATA) linux/icons/tyrian2000-32.png $(DESTDIR)$(icondir)/hicolor/32x32/apps/opentyrian2000.png
	$(INSTALL_DATA) linux/icons/tyrian2000-48.png $(DESTDIR)$(icondir)/hicolor/48x48/apps/opentyrian2000.png
	$(INSTALL_DATA) linux/icons/tyrian2000-128.png $(DESTDIR)$(icondir)/hicolor/128x128/apps/opentyrian2000.png

.PHONY : uninstall
uninstall :
	rm -f $(DESTDIR)$(bindir)/$(TARGET)
	rm -f $(DESTDIR)$(docdir)/NEWS $(DESTDIR)$(docdir)/README
	rm -f $(DESTDIR)$(man6dir)/opentyrian2000$(man6ext)
	rm -f $(DESTDIR)$(desktopdir)/opentyrian2000.desktop
	rm -f $(DESTDIR)$(icondir)/hicolor/22x22/apps/opentyrian2000.png
	rm -f $(DESTDIR)$(icondir)/hicolor/24x24/apps/opentyrian2000.png
	rm -f $(DESTDIR)$(icondir)/hicolor/32x32/apps/opentyrian2000.png
	rm -f $(DESTDIR)$(icondir)/hicolor/48x48/apps/opentyrian2000.png
	rm -f $(DESTDIR)$(icondir)/hicolor/128x128/apps/opentyrian2000.png

.PHONY : clean
clean :
	rm -f $(OBJS)
	rm -f $(DEPS)
	rm -f $(SAN_OBJS)
	rm -f $(SAN_DEPS)
	rm -f $(TARGET) $(SAN_TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $@ $^ $(ALL_LDLIBS)

$(SAN_TARGET) : $(SAN_OBJS)
	$(CC) $(ALL_CFLAGS) $(SANITIZER_FLAGS) $(ALL_LDFLAGS) -o $@ $^ $(ALL_LDLIBS)

-include $(DEPS) $(SAN_DEPS)

obj/%.o : src/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c -o $@ $<

obj/sanitize/%.o : src/%.c
	@mkdir -p "$(dir $@)"
	$(CC) $(ALL_CPPFLAGS) -UNDEBUG $(ALL_CFLAGS) $(SANITIZER_FLAGS) -c -o $@ $<
