# =============================================================================
# hypjolin-alchemy -- Hypjolin (Benjolin + Hypster) for the Hermetic Modular
# Alchemy Lab V2 (after Triglav Modular's Hypjolin; see src/hypjolin/DESIGN.md).
#
#   make libdaisy     build lib/libDaisy once after cloning
#   make              build/hypjolin_alchemy.bin (BOOT_SRAM)
#   make test         native engine suite (no hardware)
#   make size         image size against the 480 KB SRAM region
#   make stage        copy the .bin to ../daisy-sdk/alchemy-lab/ (never the card)
#
# A worktree's lib/ submodules are empty; point at the main checkout's:
#   make ALCHEMY_DIR=~/tim-os/hypjolin-alchemy/lib/alchemy-sdk \
#        LIBDAISY_DIR=~/tim-os/hypjolin-alchemy/lib/libDaisy
#
# Build flags:
#   BENCH_USB=1   HostLink on the Seed's micro-USB instead of the front USB-C.
#                 NOT for racking. BUILD_DIR defaults to build-bench.
#
# Nothing here flashes unless program-live is named. Never build with the
# module attached in DFU (memory feedback_no_builds_with_board_in_dfu).
# =============================================================================

TARGET = hypjolin_alchemy

ALCHEMY_DIR  ?= lib/alchemy-sdk
LIBDAISY_DIR ?= lib/libDaisy

ifeq ($(BENCH_USB),1)
override BUILD_DIR := build-bench
else
override BUILD_DIR := build
endif

CPP_SOURCES = src/hypjolin/hypjolin_alchemy.cpp src/hypjolin/hypjolin_engine.cpp src/common/picker.cpp
CPP_SOURCES += $(sort $(shell find $(ALCHEMY_DIR)/framework/src -name '*.cpp'))
CPP_SOURCES += $(sort $(wildcard $(ALCHEMY_DIR)/hardware/alchemy-lab/v2/src/*.cpp))

C_INCLUDES += \
    -Isrc/common \
    -Isrc/hypjolin \
    -I$(ALCHEMY_DIR)/framework/include \
    -I$(ALCHEMY_DIR)/hardware/include \
    -I$(ALCHEMY_DIR)/hardware/alchemy-lab/v2/include

C_DEFS += -DALCHEMY_BOARD_V2

HYPJOLIN_VERSION  := $(shell cat VERSION 2>/dev/null || echo 0.0.0)
HYPJOLIN_GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)
C_DEFS += -DHYPJOLIN_VERSION=\"$(HYPJOLIN_VERSION)\" -DHYPJOLIN_GIT_HASH=\"$(HYPJOLIN_GIT_HASH)\"

ifeq ($(BENCH_USB),1)
C_DEFS += -DHYPJOLIN_BENCH_USB
endif

# ── Daisy bootloader build (BOOT_SRAM), as every Alchemy firmware ────────────
USE_FATFS    = 1
APP_TYPE     = BOOT_SRAM
LDSCRIPT     = src/common/alchemy.lds
CPP_STANDARD = -std=gnu++17
OPT          = -O3

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

.DEFAULT_GOAL := all

# newlib-nano prints nothing for "%f" unless _printf_float is linked.
override LDFLAGS += -u _printf_float

# The commit hash is a -D on every compile line and make does not track
# flags: retire the object that bakes it in whenever HEAD moves.
HASH_STAMP := $(BUILD_DIR)/.hash-$(HYPJOLIN_GIT_HASH)
ifeq ($(wildcard $(HASH_STAMP)),)
_HASH_GUARD := $(shell rm -f $(BUILD_DIR)/$(TARGET).o $(BUILD_DIR)/$(TARGET).d $(BUILD_DIR)/.hash-* 2>/dev/null; mkdir -p $(BUILD_DIR); touch $(HASH_STAMP))
endif

# A linker-script change must relink (belt-alchemy's lesson).
$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)

.PHONY: libdaisy test size stage
libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR)

test:
	$(MAKE) -C test

size:
	@e=$(BUILD_DIR)/$(TARGET).elf; arm-none-eabi-size -A $$e | awk '/^\.(text|rodata|data|itcm)|isr/{s+=$$2} END{printf "image %7d B of 491520 (%.1f%%)\n", s, s*100/491520}'

STAGE_DIR ?= $(HOME)/tim-os/daisy-sdk/alchemy-lab
stage:
	cp -v $(BUILD_DIR)/$(TARGET).bin $(STAGE_DIR)/$(TARGET)-$(HYPJOLIN_GIT_HASH)-front.bin

# Reboot the running module over HostLink and flash it. Tim's step, pinned:
# dfu-util -S <serial> (memory feedback_pin_dfu_util_to_device).
USBPID ?= df11
.PHONY: program-live
program-live: all
	@test -n "$(DFU_SERIAL)" || (echo "set DFU_SERIAL (dfu-util -l lists it)"; false)
	node $(ALCHEMY_DIR)/tools/hostlink-cli/hostlink.mjs reboot bootloader
	dfu-util -w -S $(DFU_SERIAL) -a 0 -s $(FLASH_ADDRESS):leave -D $(BUILD_DIR)/$(TARGET_BIN) -d ,0483:$(USBPID)
