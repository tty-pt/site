CFLAGS += -DND_MCP_PROVIDER
all := libnd-mcp

libnd-mcp-obj-y := src/mcp.o

LDLIBS-libnd-mcp := -lxylem -lcorm -lqsys

FOLDER := nd

LDFLAGS-libnd-mcp-Darwin += -undefined dynamic_lookup
add-prefix := ../axil-nd ../nd-region ../nd-session ../nd-world ../nd-view ../nd-obj ../nd-speech ../nd-mcp

-include ./../mk/include.mk
