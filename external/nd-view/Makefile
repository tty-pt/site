all := libnd-view

libnd-view-obj-y := src/view.o src/mods.o

LDLIBS-libnd-view := -lxylem -lcorm -lislet -lstele -lnd-bus

FOLDER := nd

LDFLAGS-libnd-view-Darwin += -undefined dynamic_lookup

-include ./../mk/include.mk
