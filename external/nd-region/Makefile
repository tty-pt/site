all := libnd-region

libnd-region-obj-y := src/region_cmd.o

LDLIBS-libnd-region := -lxylem -lcorm

FOLDER := nd

LDFLAGS-libnd-region-Darwin += -undefined dynamic_lookup
add-prefix := ../axil-nd ../nd-region ../nd-session ../nd-world ../nd-view ../nd-obj ../nd-speech ../nd-mcp

-include ./../mk/include.mk
