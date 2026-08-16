include $(HOME)/mk/portable.mk

CC ?= ${cc}
PICFLAGS-Unix = -fPIC
PICFLAGS = ${PICFLAGS-${SYS}}
CFLAGS += -g -O0 $(PICFLAGS)

TARGET = libtransp.a
SRC = token.c parse.c render.c transp.c
OBJ = $(SRC:.c=.o)
HDR = transp.h transp_flags.h token.h parse.h render.h

all: $(TARGET)

$(TARGET): $(OBJ)
	ar rcs $@ $(OBJ)

$(OBJ): %.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

test_transp: test_transp.c $(TARGET)
	$(CC) $(CFLAGS) -o $@ test_transp.c ./$(TARGET) $(LDLIBS)

test: test_transp
	./test_transp

clean:
	rm -f $(OBJ) $(TARGET) test_transp

.PHONY: all clean test
