CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS += -Iinclude -Iplugins
LDLIBS += -ldl

CORE_SRCS = core/main.c core/backend.c core/plugin_loader.c core/core_api.c

PLUGIN_NAMES = memdump valuescan aobscan memwatch ptrscan
PLUGIN_SO = $(addprefix plugins/,$(addsuffix .so,$(PLUGIN_NAMES)))

all: xmod-core driver plugins

driver:
	$(MAKE) -C driver

xmod-core: $(CORE_SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(CORE_SRCS) $(LDLIBS)

plugins: $(PLUGIN_SO)

plugins/%.so: plugins/%.c include/xmod/plugin.h plugins/common.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -shared -fPIC -o $@ $<

clean:
	rm -f xmod-core $(PLUGIN_SO)
	$(MAKE) -C driver clean

.PHONY: all driver plugins clean
