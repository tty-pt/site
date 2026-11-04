all := libnd-speech

libnd-speech-obj-y := src/speech.o src/match.o src/look.o src/wiz.o

LDLIBS-libnd-speech := -lxylem -lcorm -lstele

FOLDER := nd

LDFLAGS-libnd-speech-Darwin += -undefined dynamic_lookup

-include ./../mk/include.mk
