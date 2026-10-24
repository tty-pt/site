CFLAGS += -DND_WORLD_PROVIDER
all := libnd-world

libnd-world-obj-y := src/map.o src/noise.o src/spacetime.o src/teleport.o

LDLIBS-libnd-world := -lxylem -lcorm -lislet -lqsys -lnd-bus

FOLDER := nd

LDFLAGS-libnd-world-Darwin += -undefined dynamic_lookup
add-prefix := ../axil-nd ../nd-region ../nd-session ../nd-world ../nd-view ../nd-obj ../nd-speech ../nd-mcp

-include ./../mk/include.mk
