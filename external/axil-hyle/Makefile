all := libaxil-hyle
SONAME-libaxil-hyle := axil-hyle

EXTRA_CFLAGS += -I$(shell cd .. && pwd)/axil/include \
	-I$(shell cd .. && pwd)/qmap/include \
	-I$(shell cd .. && pwd)/hyle/include \
	-I$(shell cd .. && pwd)/hyle/c/libhyle-source/include \
	-I$(shell cd .. && pwd)/hyle/c/libhyle-bud/include \
	-I$(shell cd .. && pwd)/bud/include \
	-I$(shell cd .. && pwd)/axil-auth/include

LDLIBS-libaxil-hyle := -laxil -lqmap -lhyle -lhyle-source -lhyle-bud -lbud -ljson-c -laxil-auth
LDFLAGS-libaxil-hyle := -L$(shell cd .. && pwd)/axil/lib \
	-L$(shell cd .. && pwd)/qmap/lib \
	-L$(shell cd .. && pwd)/hyle/lib \
	-L$(shell cd .. && pwd)/hyle/c/libhyle-source/lib \
	-L$(shell cd .. && pwd)/hyle/c/libhyle-bud/lib \
	-L$(shell cd .. && pwd)/bud/lib \
	-L$(shell cd .. && pwd)/axil-auth/lib \
	-Wl,-rpath,$(shell cd .. && pwd)/axil/lib \
	-Wl,-rpath,$(shell cd .. && pwd)/hyle/c/libhyle-source/lib \
	-Wl,-rpath,$(shell cd .. && pwd)/hyle/c/libhyle-bud/lib

CFLAGS += -fPIC $(EXTRA_CFLAGS)

include ../mk/include.mk
