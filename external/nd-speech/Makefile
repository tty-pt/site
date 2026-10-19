all := libnd-speech

libnd-speech-obj-y := src/speech.o src/match.o src/look.o src/wiz.o

LDLIBS-libnd-speech := -lxylem -lcorm -lqsys -L../axil-nd/lib -laxil-nd

FOLDER := nd

LDFLAGS-libnd-speech-Darwin += -undefined dynamic_lookup
add-prefix-glob := ../nd-* ../axil-nd

-include ./../mk/include.mk
