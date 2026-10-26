all := libnd-bus

libnd-bus-obj-y := src/nd_events.o

LDLIBS-libnd-bus := -lxylem -lcorm -lqsys

FOLDER := nd
add-prefix := ../axil-nd ../nd-region ../nd-session ../nd-world ../nd-view ../nd-obj ../nd-speech ../nd-mcp

LDFLAGS-libnd-bus-Darwin += -undefined dynamic_lookup
-include ./../mk/include.mk
