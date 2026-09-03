# Nintendo Switch homebrew build, based on the devkitPro libnx template.
# Requires switch-dev, switch-sdl2, and switch-sdl2_net. Run `make` here.
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

# Homebrew Launcher metadata
APP_TITLE   := OpenTyrian 2000 Engaged
APP_AUTHOR  := wlfn / OpenTyrian Team

# Homebrew Launcher version from src/opentyrian_version.h, trimmed to the NACP limit.
APP_VERSION := $(shell sed -n 's/.*OPENTYRIAN_VERSION *"\([^"]*\)".*/\1/p' $(TOPDIR)/../src/opentyrian_version.h | sed 's/.*[vV]//')
ifeq ($(strip $(APP_VERSION)),)
    APP_VERSION := 0.0.0
endif

# Output name, object directory, source/include roots, and read-only romfs root.
TARGET   := opentyrian2000
BUILD    := build
SOURCES  := ../src
INCLUDES := ../src
ROMFS    := romfs

# Code generation
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# Resolve switch-portlibs directly and request its static SDL2 dependencies.
# See switch/README.md if the wrapper is unavailable.
PKGCONF := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config

# Match desktop Release, retain symbols, and preserve engine/netplay assumptions.
CFLAGS := -g -Wall -Wno-format-truncation -O2 -DNDEBUG -ffunction-sections -fsigned-char \
          -ffp-contract=off $(ARCH)

# TYRIAN_DIR is a fallback behind the sdmc:/ and romfs: paths. WITH_NETWORK uses
# switch-sdl2_net and the socket initialization in switch_platform.c.
CFLAGS += $(INCLUDE) -D__SWITCH__ -DWITH_NETWORK -DTYRIAN_DIR='"."' $(shell $(PKGCONF) sdl2 SDL2_net --cflags)

# CI supplies the title-screen commit id; local builds ask git.
OPENTYRIAN_COMMIT ?= $(shell git -C $(TOPDIR)/.. rev-parse --short HEAD 2>/dev/null)
ifneq ($(strip $(OPENTYRIAN_COMMIT)),)
    CFLAGS += -DOPENTYRIAN_COMMIT='"$(OPENTYRIAN_COMMIT)"'
endif

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# SDL2 and its full (static) dependency chain, then libnx/libm as a safe backstop.
# SDL2_net comes first so the static linker resolves its SDL2 references.
LIBS := $(shell $(PKGCONF) sdl2 SDL2_net --libs --static) -lnx -lm

# Library roots containing include/ and lib/
LIBDIRS := $(PORTLIBS) $(LIBNX)

# Standard devkitPro recursive-build rules
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

# Select the linker from the source language.
ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_SRC)
export HFILES     := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.h))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

# Main targets
all: $(OUTPUT).nro

# switch_rules builds the NRO from the ELF, NACP metadata, and flat romfs contents.
ROMFS_FILES := $(wildcard $(TOPDIR)/$(ROMFS)/*)
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp $(ROMFS_FILES)
$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES)

-include $(DEPENDS)

endif
