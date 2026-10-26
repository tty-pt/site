CFLAGS += -DND_SESSION_PROVIDER
all := libnd-session

libnd-session-obj-y := src/session.o

LDLIBS-libnd-session := -lxylem -lcorm -lqsys -laxil-tty -laxil-nd -lnd-bus

FOLDER := nd

LDFLAGS-libnd-session-Darwin += -undefined dynamic_lookup
add-prefix := ../axil-nd ../nd-region ../nd-session ../nd-world ../nd-view ../nd-obj ../nd-speech ../nd-mcp

-include ./../mk/include.mk
