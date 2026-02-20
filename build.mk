REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
MOD_NAME := $(notdir $(CURDIR))

ifneq (,$(findstring /modules/,$(CURDIR)))
TARGET ?= be/nd_$(MOD_NAME).so
SRC ?= be/nd_$(MOD_NAME).c
else
TARGET ?= $(MOD_NAME).so
SRC ?= $(MOD_NAME).c
endif

CC ?= clang
NDX_PREFIX ?= /home/quirinpa/ndx
NDC_PREFIX ?= /home/quirinpa/ndc
QMAP_PREFIX ?= /home/quirinpa/qmap

CFLAGS ?= -g -O0 -fPIC
CFLAGS += -I$(NDX_PREFIX)/include -I$(NDX_PREFIX)/src \
	-I$(NDC_PREFIX)/include -I$(QMAP_PREFIX)/include -I/usr/include/ttypt \
	$(EXTRA_CFLAGS)

LDFLAGS ?= -shared
LDFLAGS += -L$(NDC_PREFIX)/lib -L$(QMAP_PREFIX)/lib \
	-Wl,-rpath,$(NDC_PREFIX)/lib -Wl,-rpath,$(QMAP_PREFIX)/lib

LDLIBS ?= -lndc -lqmap
LDLIBS += $(EXTRA_LDLIBS)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
