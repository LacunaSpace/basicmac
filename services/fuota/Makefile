CFLAGS += -Wall -g
CFLAGS += -std=gnu11
CFLAGS += -MMD -MP -std=gnu11

CFLAGS += -DFUOTA_HAL_IMPL='"fuota_hal_x86_64.h"'
CFLAGS += -DFUOTA_GENERATOR

OBJS := test.o fuota.o

test: $(OBJS)

clean:
	rm -f *.o *.d test

.PHONY: clean

-include $(OBJS:.o=.d)
