CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS += -Iinclude

CORE_SRCS = core/main.c core/backend.c

all: xmod-core driver

driver:
	$(MAKE) -C driver

xmod-core: $(CORE_SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(CORE_SRCS)

clean:
	rm -f xmod-core
	$(MAKE) -C driver clean

.PHONY: all driver clean
