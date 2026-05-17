# Project Name
TARGET = ott_patch

# Sources
CPP_SOURCES = src/ott_patch.cpp src/profile.cpp

# Local include paths so the flat `#include "foo.h"` style keeps working
# regardless of which subfolder a header lives in. Must be C_INCLUDES (not
# a bare -I in CPPFLAGS): libDaisy's core Makefile does `C_INCLUDES ?=`
# then appends its own, so a project `C_INCLUDES +=` set before the
# include survives and is folded into CFLAGS/CPPFLAGS.
C_INCLUDES += -Isrc -Isrc/config -Isrc/dsp

# Enable the audio-callback CPU profiler (see profile.h). Comment out to
# disable. NOTE: must be C_DEFS, not CPPFLAGS -- libDaisy's core Makefile
# does `CPPFLAGS = $(CFLAGS)` after this include, which would discard a
# user CPPFLAGS. C_DEFS is pulled into CFLAGS (and thus CPPFLAGS) and uses
# += so it survives.
#C_DEFS += -DOTT_PROFILE_ENABLED

# Ad-hoc defines passed on the command line, e.g.:
#   make OTT_EXTRA_DEFS=-DUSE_FAST_MATH
#   make OTT_EXTRA_DEFS="-DUSE_FAST_MATH -DSOMETHING_ELSE"
# Folded into C_DEFS here. Do NOT use `make C_DEFS=...` directly -- a
# command-line C_DEFS overrides and wipes libDaisy's own C_DEFS += lines.
# OTT_EXTRA_DEFS is not set in-Makefile, so the command-line value flows
# through cleanly while libDaisy's C_DEFS accumulation is preserved.
# Still requires `make clean` when toggling: object timestamps don't
# change just because a -D did.
C_DEFS += $(OTT_EXTRA_DEFS)

# Fast-math kernels: by DEFAULT FastLog2/FastExp2 use the libm reference
# path (log2f/exp2f, see fast_math.h). Define USE_FAST_MATH to switch to
# the IEEE-754 bit-trick approximations. Production builds that want the
# speed must opt in: uncomment the line below for a sticky setting, or
# pass it per build via OTT_EXTRA_DEFS above.
# C_DEFS += -DUSE_FAST_MATH

# Library Locations
LIBDAISY_DIR = ../libDaisy
DAISYSP_DIR  = ../DaisySP

# Core Makefile (provides toolchain, flags, link, program targets)
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
