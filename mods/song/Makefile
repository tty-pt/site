EXTRA_CFLAGS += -I./lib/transp
EXTRA_LDLIBS += ./lib/transp/libtransp.a

all: transp.so $(TARGET)

transp.so:
	${MAKE} -C lib/transp

include ../../build.mk
