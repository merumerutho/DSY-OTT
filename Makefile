# Project Name
TARGET = ott_patch

# Sources
CPP_SOURCES = src/ott_patch.cpp src/profile.cpp

# Local include paths
C_INCLUDES += -Isrc -Isrc/config -Isrc/dsp

# Header-only PagedControls library (pages + soft-takeover knob pickup).
C_INCLUDES += -Ilib/PagedControls

# Enable the audio-callback CPU profiler (see profile.h). 
C_DEFS += -DOTT_PROFILE_ENABLED

# Fast-math kernels (log/exp)
C_DEFS += -DUSE_FAST_MATH
# Fast-math kernels (inverse)
C_DEFS += -DOTT_FAST_SOFTCLIP

# Disable oversampling
# Use this flag to disable it
# C_DEFS += -DOTT_NO_CLIP_OVERSAMPLE

# Ad-hoc defines passed on the command line
C_DEFS += $(OTT_EXTRA_DEFS)

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR  = ../../DaisySP

# Core Makefile (provides toolchain, flags, link, program targets)
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
