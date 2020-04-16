# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

VARIANTS ?= eu868

RVARIANT	:= $(VARIANT)

ifeq ($(VARIANT),)
    VTARGET	:= variants
    VARIANT	:= $(firstword $(DEFAULT_VARIANT) $(VARIANTS))
else
    VTARGET	:= all
endif

BUILDDIR	= $(BUILDDIR_PFX)$(VARIANT)
VTARGETS	:= $(addprefix variant-,$(VARIANTS))

# settings for "well-known" variants

REGIONS.eu868	:= eu868
REGIONS.us915	:= us915
REGIONS.in865	:= in865

REGIONS.hybrid	?= eu868 us915

ifneq (,$(REGIONS.$(VARIANT)))
    REGIONS = $(REGIONS.$(VARIANT))
endif

ifneq (,$(TARGET.$(VARIANT)))
    TARGET = $(TARGET.$(VARIANT))
endif
