# The Benjolin build on the alchemy-lab emulator (branch emu-hostlink). This
# folder is its own FW_DIR, so only its emu-tests/ run against it (the repo
# root's emu.mk and emu-tests/ are Hypjolin's):
#
#   make -C <alchemy-lab>/emu test-custom FW=custom FW_DIR=<repo>/src/benjolin \
#        EMU_ALCHEMY_DIR=<repo>/lib/alchemy-sdk EMU_LIBDAISY_DIR=<repo>/lib/libDaisy
#
# Paths are relative to this folder. picker.cpp stays out: the emulator stubs
# the SD picker itself.
include $(FW_DIR)/name.mk
EMU_TARGET       := $(BENJOLIN_TARGET)
EMU_CPP_SOURCES  := benjolin_alchemy.cpp ../hypjolin/hypjolin_engine.cpp
EMU_C_SOURCES    :=
EMU_C_INCLUDES   := -I. -I../hypjolin -I../common
EMU_C_DEFS       := -DBENJOLIN_TARGET=\"$(BENJOLIN_TARGET)\" -DBENJOLIN_PRODUCT=\"$(BENJOLIN_PRODUCT)\" \
                    -DBENJOLIN_VERSION=\"emu\" -DBENJOLIN_GIT_HASH=\"emu\"
EMU_ALCHEMY_DIR  ?= ../../lib/alchemy-sdk
EMU_LIBDAISY_DIR ?= ../../lib/libDaisy
EMU_LABELS       := OSC A|OSC B|RUN>A|RUN>B|RUN>F|LOOP
EMU_LABELS_ALT   := >OSC A|OSC A CV|>OSC B|OSC B CV|>FILTER|FILTER CV
