CFLAGS += -DND_OBJ_PROVIDER
all := libnd-obj

libnd-obj-obj-y := src/item.o src/object.o src/entity.o

LDLIBS-libnd-obj := -lxylem -lcorm -lislet -lqsys -lnd-bus

FOLDER := nd

LDFLAGS-libnd-obj-Darwin += -undefined dynamic_lookup
add-prefix := ../axil-nd ../nd-region ../nd-session ../nd-world ../nd-view ../nd-obj ../nd-speech ../nd-mcp

-include ./../mk/include.mk
