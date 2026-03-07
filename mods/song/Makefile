EXTRA_CFLAGS += -I./lib/transp
EXTRA_LDLIBS += -L./lib/transp -ltransp -Wl,-rpath,'$$ORIGIN/lib/transp'

all: transp.so song.so

include ../../build.mk

transp.so:
	${MAKE} -C lib/transp

