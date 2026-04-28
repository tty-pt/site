include $(HOME)/mk/portable.mk

CC ?= ${cc}
PICFLAGS-Unix = -fPIC
PICFLAGS = ${PICFLAGS-${SYS}}
CFLAGS += -g -O0 $(PICFLAGS)
LDLIBS += -lqmap

TARGET = libtransp.a
SRC = transp.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	ar rcs $@ $(OBJ)

$(OBJ): $(SRC) transp.h
	$(CC) $(CFLAGS) -c -o $@ $(SRC)

test_transp: test_transp.c $(TARGET)
	$(CC) $(CFLAGS) -o $@ test_transp.c ./$(TARGET) $(LDLIBS)

test: test_transp
	./test_transp

clean:
	rm -f $(OBJ) $(TARGET) test_transp

.PHONY: all clean test
