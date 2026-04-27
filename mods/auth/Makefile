include ../../build.mk

ifneq ($(uname),OpenBSD)
EXTRA_LDLIBS += -lcrypt
endif

test:
	./test.sh
