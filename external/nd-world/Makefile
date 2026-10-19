CFLAGS += -DND_WORLD_PROVIDER
all := libnd-world

libnd-world-obj-y := src/map.o src/noise.o src/spacetime.o src/teleport.o

LDLIBS-libnd-world := -lxylem -lcorm -lislet -lqsys -L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -Wl,--as-needed

FOLDER := nd

LDFLAGS-libnd-world-Darwin += -undefined dynamic_lookup
add-prefix-glob := ../nd-* ../axil-nd

-include ./../mk/include.mk
