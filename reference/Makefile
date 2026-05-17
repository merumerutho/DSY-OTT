# Project Name
TARGET = ott_patch

# Sources
CPP_SOURCES = ott_patch.cpp profile.cpp

# Uncomment to enable the audio-callback CPU profiler (see profile.h).
# CPPFLAGS += -DOTT_PROFILE_ENABLED

# Library Locations (now one level deeper after the custom_OTT/ move)
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR  = ../../DaisySP

# Core Makefile (provides toolchain, flags, link, program targets)
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
