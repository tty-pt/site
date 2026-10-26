all := libaxil-embed-search
SONAME-libaxil-embed-search := axil-embed-search

LDLIBS-libaxil-embed-search := -laxil -lxylem -lcorm -lsepal -llemma -ljson-c
LDLIBS-libaxil-embed-search-Linux := -ldl
LDFLAGS-libaxil-embed-search-Darwin := -undefined dynamic_lookup

AXIL_ROOT := $(shell cd .. && pwd)
CFLAGS += -Iinclude \
          -I$(AXIL_ROOT)/axil/include \
          -I$(AXIL_ROOT)/libcorm/include \
          -I$(AXIL_ROOT)/libsepal/include \
          -I$(AXIL_ROOT)/liblemma/include \
          -I$(AXIL_ROOT)/libxylem/include \
          -I/usr/include/json-c

-include ./../mk/include.mk

test:
	@if [ -x ./test.sh ]; then \
		./test.sh; \
	else \
		echo "axil-embed-search: test.sh not executable"; \
	fi
