CFLAGS += -DND_OBJ_PROVIDER
all := libnd-obj

libnd-obj-obj-y := src/item.o src/object.o src/entity.o

LDLIBS-libnd-obj := -lxylem -lcorm -lislet -lqsys -L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-region/lib -lnd-region -L../nd-bus/lib -lnd-bus -Wl,--as-needed

FOLDER := nd

LDFLAGS-libnd-obj-Darwin += -undefined dynamic_lookup
add-prefix-glob := ../nd-* ../axil-nd

-include ./../mk/include.mk
