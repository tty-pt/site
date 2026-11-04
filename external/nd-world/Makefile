CFLAGS += -DND_WORLD_PROVIDER
all := libnd-world

libnd-world-obj-y := src/map.o src/noise.o src/spacetime.o src/teleport.o

LDLIBS-libnd-world := -lxylem -lcorm -lislet -lstele -lnd-bus

FOLDER := nd

LDFLAGS-libnd-world-Darwin += -undefined dynamic_lookup

-include ./../mk/include.mk
