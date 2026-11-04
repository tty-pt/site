CFLAGS += -DND_SESSION_PROVIDER
all := libnd-session

libnd-session-obj-y := src/session.o

LDLIBS-libnd-session := -lxylem -lcorm -lstele -laxil-tty -laxil-nd -lnd-bus

FOLDER := nd

LDFLAGS-libnd-session-Darwin += -undefined dynamic_lookup

-include ./../mk/include.mk
