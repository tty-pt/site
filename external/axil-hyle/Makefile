all := libaxil-hyle
SONAME-libaxil-hyle := axil-hyle

PARENT != cd .. && pwd

EXTRA_CFLAGS += -I${PARENT}/axil/include \
	-I${PARENT}/libqmap/include \
	-I${PARENT}/hyle/include \
	-I${PARENT}/hyle/c/libhyle-source/include \
	-I${PARENT}/hyle/c/libhyle-bud/include \
	-I${PARENT}/bud/include \
	-I${PARENT}/axil-auth/include

LDLIBS-libaxil-hyle := -laxil -lqmap -lhyle -lhyle-source -lhyle-bud -lbud -ljson-c -laxil-auth
LDFLAGS-libaxil-hyle := -L${PARENT}/axil/lib \
	-L${PARENT}/libqmap/lib \
	-L${PARENT}/hyle/lib \
	-L${PARENT}/hyle/c/libhyle-source/lib \
	-L${PARENT}/hyle/c/libhyle-bud/lib \
	-L${PARENT}/bud/lib \
	-L${PARENT}/axil-auth/lib \
	-Wl,-rpath,${PARENT}/axil/lib \
	-Wl,-rpath,${PARENT}/hyle/c/libhyle-source/lib \
	-Wl,-rpath,${PARENT}/hyle/c/libhyle-bud/lib \
	-Wl,-rpath,${PARENT}/bud/lib

CFLAGS += -fPIC $(EXTRA_CFLAGS)

include ../mk/include.mk
