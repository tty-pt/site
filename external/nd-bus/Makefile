all := libnd-bus

libnd-bus-obj-y := src/nd_events.o

LDLIBS-libnd-bus := -lxylem -lcorm -lqsys

FOLDER := nd
add-prefix-glob := ../nd-* ../axil-nd

LDFLAGS-libnd-bus-Darwin += -undefined dynamic_lookup
-include ./../mk/include.mk
