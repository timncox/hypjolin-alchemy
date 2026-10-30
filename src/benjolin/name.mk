# The Benjolin build's name, in ONE place. Read by the repo Makefile
# (make FW=benjolin) and by src/benjolin/emu.mk (the emulator).
#
#   BENJOLIN_TARGET   the .bin's name, HostLink's firmware id and the
#                     autosave file 0:/alchemy/state/<BENJOLIN_TARGET>.st
#   BENJOLIN_PRODUCT  the name the web programmer and the manual show
#
# "benjolin_alchemy" is a working name (2026-10-30), not chosen yet.
# Renaming it moves the autosave file, so a module keeps its panel only if
# the old .st is renamed on the card too.
BENJOLIN_TARGET  := benjolin_alchemy
BENJOLIN_PRODUCT := Benjolin
BENJOLIN_VERSION := 0.1.0
