# The alchemy-lab emulator (branch emu-custom / emu-hostlink):
#
#   make -C <alchemy-lab>/emu FW=custom FW_DIR=<this repo>
#   make -C <alchemy-lab>/emu test-custom FW_DIR=<this repo>   (emu-tests/*.emu)
#
# picker.cpp stays out: the emulator stubs the SD picker itself.
EMU_TARGET       := hypjolin_alchemy
EMU_CPP_SOURCES  := src/hypjolin/hypjolin_alchemy.cpp src/hypjolin/hypjolin_engine.cpp
EMU_C_SOURCES    :=
EMU_C_INCLUDES   := -Isrc/hypjolin -Isrc/common
EMU_C_DEFS       := -DHYPJOLIN_VERSION=\"emu\" -DHYPJOLIN_GIT_HASH=\"emu\"
EMU_ALCHEMY_DIR  ?= lib/alchemy-sdk
EMU_LIBDAISY_DIR ?= lib/libDaisy
EMU_LABELS       := OSC A|OSC B|RUN>A|RUN>B|RUN>F|LOOP
EMU_LABELS_ALT   := OSC A CV|OSC B CV|FILTER CV|RATE CV|GAIN CV|DAMP CV
# (On the emu-hostlink emulator the labels come from the firmware's own
# VirtualKnob names on whatever page is showing; these are the fallback.)
