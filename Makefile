CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS += -Iinclude -Iplugins
LDLIBS += -ldl

# GTK3 Flags for the GUI
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)

CORE_SRCS = core/backend.c
CLI_SRCS = core/main.c core/core_api.c core/plugin_loader.c
GUI_SRCS = gui/main.c

PLUGIN_NAMES = memdump valuescan aobscan memwatch ptrscan
PLUGIN_SO = $(addprefix plugins/,$(addsuffix .so,$(PLUGIN_NAMES)))

all: xmod-core Xmod driver plugins

driver:
	$(MAKE) -C driver

xmod-core: $(CORE_SRCS) $(CLI_SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(LDLIBS)

Xmod: $(CORE_SRCS) $(GUI_SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(GTK_CFLAGS) -o $@ $^ $(GTK_LIBS) $(LDLIBS)

plugins: $(PLUGIN_SO)

plugins/%.so: plugins/%.c include/xmod/plugin.h plugins/common.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -shared -fPIC -o $@ $<

clean:
	rm -f xmod-core Xmod $(PLUGIN_SO)
	$(MAKE) -C driver clean

.PHONY: all driver plugins clean
