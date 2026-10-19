all := libnd-view

libnd-view-obj-y := src/view.o src/mods.o

LDLIBS-libnd-view := -lxylem -lcorm -lislet -lqsys -L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -L../nd-world/lib -lnd-world -Wl,--as-needed

FOLDER := nd

LDFLAGS-libnd-view-Darwin += -undefined dynamic_lookup
add-prefix-glob := ../nd-* ../axil-nd

-include ./../mk/include.mk
