CFLAGS += -DND_SESSION_PROVIDER
all := libnd-session

libnd-session-obj-y := src/session.o

LDLIBS-libnd-session := -lxylem -lcorm -lqsys -laxil-tty -L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -L../nd-view/lib -lnd-view -Wl,--as-needed

FOLDER := nd

LDFLAGS-libnd-session-Darwin += -undefined dynamic_lookup
add-prefix-glob := ../nd-* ../axil-nd

-include ./../mk/include.mk
