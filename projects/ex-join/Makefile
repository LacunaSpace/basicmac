TARGET := b_l072z_lrwan1
#TARGET := nucleo_l073rz-sx1276mb1las
#TARGET := nucleo_l073rz-sx1272mbed
#TARGET := nucleo_l073rz-sx1276mb1mas
#TARGET := nucleo_l053r8-sx1276mb1las
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

DEFS += -DSVC_FRAG_TEST  # enable proprietary test command in frag service

DEFS += -DSVC_FWMAN_TEST # enable test key function in fwman service
DEFS += -DSVC_FWMAN_PUBKEY=fwman_testkey
DEFS += -DSVC_FWMAN_CURVE=uECC_secp256r1

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

apptest: build-$(VARIANT)/$(PROJECT).hex
	PYTHONPATH=$${PYTHONPATH}:$(TOPDIR)/tools/pylora:$(TOPDIR)/unicorn/simul \
		   ./test.py \
		   -r EU868 \
		   -v -d -t \
		   $(TESTOPTS) \
		   $(BL)/build/boards/$(BL_BRD)/bootloader.hex \
		   $<

test.up: build-$(VARIANT)/$(PROJECT).zfw
	$(ZFWTOOL) mkupdate $< $@

fuotatest: build-$(VARIANT)/$(PROJECT).hex
	PYTHONPATH=$${PYTHONPATH}:$(TOPDIR)/tools/pylora:$(TOPDIR)/unicorn/simul:$(TOPDIR)/basicloader/tools/fwtool \
		   ZFWFILE=$(<:.hex=.zfw) \
		   $(TOPDIR)/services/fuota/test.py \
		   -r EU868 \
		   -v -d -t \
		   $(TESTOPTS) \
		   $(BL)/build/boards/$(BL_BRD)/bootloader.hex \
		   $<
endif


.PHONY: test apptest fuotatest
