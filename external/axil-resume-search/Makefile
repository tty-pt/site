all := libaxil-resume-search
SONAME-libaxil-resume-search := axil-resume-search

LDLIBS-libaxil-resume-search := -laxil -lxylem -lcorm -lsepal -llemma -ljson-c
LDLIBS-libaxil-resume-search-Linux := -ldl
LDFLAGS-libaxil-resume-search-Darwin := -undefined dynamic_lookup

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
		echo "axil-resume-search: test.sh not executable"; \
	fi
	@if [ -x ./eval_matches.sh ]; then \
		./eval_matches.sh; \
	fi

eval:
	@if [ -x ./eval_matches.sh ]; then \
		./eval_matches.sh; \
	else \
		echo "axil-resume-search: eval_matches.sh not executable"; \
	fi
