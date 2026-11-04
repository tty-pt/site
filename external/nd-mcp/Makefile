CFLAGS += -DND_MCP_PROVIDER
all := libnd-mcp

libnd-mcp-obj-y := src/mcp.o

LDLIBS-libnd-mcp := -lxylem -lcorm -lstele

FOLDER := nd

LDFLAGS-libnd-mcp-Darwin += -undefined dynamic_lookup

-include ./../mk/include.mk
