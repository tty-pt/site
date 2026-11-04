CFLAGS += -DND_OBJ_PROVIDER
all := libnd-obj

libnd-obj-obj-y := src/item.o src/object.o src/entity.o

LDLIBS-libnd-obj := -lxylem -lcorm -lislet -lstele -lnd-bus

FOLDER := nd

LDFLAGS-libnd-obj-Darwin += -undefined dynamic_lookup

-include ./../mk/include.mk
