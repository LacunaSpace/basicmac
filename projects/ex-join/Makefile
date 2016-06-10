TARGET := b_l072z_lrwan1
#TARGET := nucleo_l053r8-sx1276mbed
#TARGET := nucleo_l053r8-sx1261mbed
#TARGET := nucleo_l053r8-sx1262mbed

VARIANTS := eu868 us915 hybrid simul

REGIONS.simul := eu868
TARGET.simul := unicorn


CFLAGS += -Os
CFLAGS += -g
CFLAGS += -Wall -Wno-main

SVCS += app

DEFS += -DDEBUG_RX
DEFS += -DDEBUG_TX
DEFS += -DUSE_IDEETRON_AES

LMICCFG += eeprom_keys
LMICCFG += DEBUG
LMICCFG += extapi

include ../projects.gmk

ifeq (simul,$(VARIANT))
test: build-$(VARIANT)/$(PROJECT).hex
	PYTHONPATH=$${PYTHONPATH}:$(TOPDIR)/tools/pylora:$(TOPDIR)/unicorn/simul \
		   $(TOPDIR)/unicorn/simul/lwtest.py \
		   -r EU868 \
		   -v -d -t \
		   $(TESTOPTS) \
		   $(BL)/build/boards/$(BL_BRD)/bootloader.hex \
		   $<
endif


.PHONY: test
