# Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

# ------------------------------------------------
# Target
# (hyphen-concatenated list of families)

FAMILIES	:= $(subst -, ,$(TARGET))


# ------------------------------------------------
# Families

FAM_MAKEFILES += $(wildcard $(TOPDIR)/projects/fam-*.mk)
-include $(FAM_MAKEFILES)


# ------------------------------------------------
# MCUs

ifeq ($(MCU:stm32%=stm32),stm32)
    TOOLCHAIN	:= gcc
    CROSS_COMPILE:=arm-none-eabi-
    CFLAGS	+= -fno-common -fno-builtin -fno-exceptions -ffunction-sections -fdata-sections -fomit-frame-pointer
    HALDIR	:= $(TOPDIR)/stm32
    CMSIS	:= $(TOPDIR)/stm32/CMSIS
    CFLAGS	+= -I$(CMSIS)/Include
    CFLAGS	+= -I$(BL)/src/common
    CFLAGS	+= -I$(BL)/src/arm/stm32lx
    CFLAGS	+= -DHAL_IMPL_INC=\"hal_stm32.h\"
    LDFLAGS	+= -nostartfiles
    LDFLAGS	+= $(addprefix -T,$(LD_SCRIPTS))
    OOCFGS	+= $(TOOLSDIR)/openocd/flash.cfg
ifeq ($(MCU),stm32l0)
    FLAGS	+= -mcpu=cortex-m0plus -mthumb
    CFLAGS	+= -I$(CMSIS)/Device/ST/STM32L0xx/Include/
    LD_SCRIPTS	+= $(HALDIR)/fw.ld
    OBJS_BLACKLIST += blipper.o
    OBJS_BLACKLIST += spi.o # these do not build yet for L0
endif
ifeq ($(MCU),stm32l1)
    FLAGS	+= -mcpu=cortex-m3 -mthumb
    CFLAGS	+= -I$(CMSIS)/Device/ST/STM32L1xx/Include/
    LD_SCRIPTS	+= STM32L1.ld
    OBJS_BLACKLIST += i2c.o adc.o # these do not build yet for L1
endif
    ALL		+= $(BUILDDIR)/$(PROJECT).hex
    ALL		+= $(BUILDDIR)/$(PROJECT).bin
    ALL		+= $(BUILDDIR)/$(PROJECT).zfw
    LOAD	 = loadhex
endif

ifeq ($(MCU),unicorn)
    TOOLCHAIN	:= gcc
    CROSS_COMPILE:=arm-none-eabi-
    HALDIR	:= $(TOPDIR)/unicorn
    FLAGS	+= -mcpu=cortex-m0plus -mthumb
    CFLAGS	+= -fno-common -fno-builtin -fno-exceptions -ffunction-sections -fdata-sections -fomit-frame-pointer
    CFLAGS	+= -I$(BL)/src/common
    CFLAGS	+= -I$(BL)/src/arm/unicorn
    CFLAGS	+= -DHAL_IMPL_INC=\"hal_unicorn.h\"
    LDFLAGS	+= -nostartfiles
    LDFLAGS	+= $(addprefix -T,$(LD_SCRIPTS))
    LD_SCRIPTS	+= $(HALDIR)/fw.ld
    ALL		+= $(BUILDDIR)/$(PROJECT).hex
    LOAD	 = dummy
    OBJS_BLACKLIST += radio.o
endif


# ------------------------------------------------
# Build tools

BL ?= $(TOPDIR)/basicloader
BL_BRD ?= $(error "No basic loader board set")
BL_BUILD ?= $(BL)/build/boards/$(BL_BRD)

ifneq (ok,$(shell python3 -c 'import sys; print("ok" if sys.hexversion >= 0x03060000 else "")'))
    $(error "Python 3.6 or newer required")
endif

SVCTOOL = $(TOOLSDIR)/svctool/svctool.py

FWTOOL = $(BL)/tools/fwtool/fwtool.py
ZFWTOOL = $(BL)/tools/fwtool/zfwtool.py


# ------------------------------------------------
# Miscellaneous settings

ifeq ($(TOOLCHAIN),gcc)
    CFLAGS	+= -MMD -MP -std=gnu11
    LDFLAGS	+= -Wl,--gc-sections -Wl,-Map,$(basename $@).map
    CC		:= $(CROSS_COMPILE)gcc
    AS		:= $(CROSS_COMPILE)as
    LD		:= $(CROSS_COMPILE)gcc
    HEX		:= $(CROSS_COMPILE)objcopy -O ihex
    BIN		:= $(CROSS_COMPILE)objcopy -O binary
    GDB		:= $(CROSS_COMPILE)gdb
    AR		:= $(CROSS_COMPILE)ar
    OPENOCD	?= openocd
endif


# ------------------------------------------------
# vim: filetype=make
