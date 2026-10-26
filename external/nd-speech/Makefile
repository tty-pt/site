all := libnd-speech

libnd-speech-obj-y := src/speech.o src/match.o src/look.o src/wiz.o

LDLIBS-libnd-speech := -lxylem -lcorm -lqsys

FOLDER := nd

LDFLAGS-libnd-speech-Darwin += -undefined dynamic_lookup
add-prefix := ../axil-nd ../nd-region ../nd-session ../nd-world ../nd-view ../nd-obj ../nd-speech ../nd-mcp

-include ./../mk/include.mk
