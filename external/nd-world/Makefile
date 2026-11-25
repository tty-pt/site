CFLAGS += -DND_WORLD_PROVIDER -DND_FIELD_MEMO_EXTERN -I../libchaff/include -I../libcorolla/include -Wno-pedantic -Wno-override-init
all := libnd-world

libnd-world-obj-y := src/map.o src/noise.o src/spacetime.o src/teleport.o

LDLIBS-libnd-world := -lxylem -lcorm -lislet -lstele -lnd-bus -lm

FOLDER := nd

LDFLAGS-libnd-world-Darwin += -undefined dynamic_lookup

-include ./../mk/include.mk

test:
	sh ../../tests/unit/run-test-map-noise.sh
