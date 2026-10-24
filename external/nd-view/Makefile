all := libnd-view

libnd-view-obj-y := src/view.o src/mods.o

LDLIBS-libnd-view := -lxylem -lcorm -lislet -lqsys -lnd-bus

FOLDER := nd

LDFLAGS-libnd-view-Darwin += -undefined dynamic_lookup
add-prefix := ../axil-nd ../nd-region ../nd-session ../nd-world ../nd-view ../nd-obj ../nd-speech ../nd-mcp

-include ./../mk/include.mk
