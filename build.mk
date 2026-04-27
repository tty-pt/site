BUILD_MK := $(abspath $(lastword $(MAKEFILE_LIST)))
BUILD_MK_DIR := $(dir $(BUILD_MK))
REPO_ROOT := $(BUILD_MK_DIR)
MOD_NAME := $(notdir $(CURDIR))

include $(HOME)/mk/portable.mk

MAKEFILE_DEPS := $(MAKEFILE_LIST)

ifneq (,$(findstring /modules/,$(CURDIR)))
TARGET ?= be/nd_$(MOD_NAME).so
SRC ?= be/nd_$(MOD_NAME).c
else
TARGET ?= $(MOD_NAME).so
SRC ?= $(MOD_NAME).c
endif

CC ?= clang

CFLAGS += -g -O0 -fPIC
CFLAGS += $(EXTRA_CFLAGS)

LDFLAGS += -shared

LDLIBS += -lndc -lqmap
LDLIBS += $(EXTRA_LDLIBS)
LDLIBS += $(EXTRA_LDLIBS-$(uname))

all: dirs $(TARGET)

dirs:
	@for d in $(DIRS); do mkdir -p $(REPO_ROOT)/$$d 2>/dev/null || true; done

$(TARGET): $(SRC) $(MAKEFILE_DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGET)

distclean: clean

.PHONY: all clean distclean
