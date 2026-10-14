CFLAGS += -DND_MCP_PROVIDER
all := libnd-mcp

libnd-mcp-obj-y := src/mcp.o

LDLIBS-libnd-mcp := -lxylem -lcorm -lqsys -L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-obj/lib -lnd-obj -Wl,--as-needed

FOLDER := nd

LDFLAGS-libnd-mcp-Darwin += -undefined dynamic_lookup
-include ./../mk/include.mk
