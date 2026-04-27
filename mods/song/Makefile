EXTRA_CFLAGS += -I./lib/transp
EXTRA_LDLIBS += ./lib/transp/libtransp.a

all: transp.so song.so

include ../../build.mk

transp.so:
	${MAKE} -C lib/transp
