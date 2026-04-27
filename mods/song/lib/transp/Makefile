include ../../../../../mk/portable.mk

CC ?= ${cc}
CFLAGS += -g -O0 -fPIC
LDLIBS += -lqmap

TARGET = libtransp.a
SRC = transp.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	ar rcs $@ $^

%.o: %.c transp.h
	$(CC) $(CFLAGS) -c -o $@ $<

test_transp: test_transp.c $(TARGET)
	$(CC) $(CFLAGS) -o $@ $< ./$(TARGET) $(LDLIBS)

test: test_transp
	./test_transp

clean:
	rm -f $(OBJ) $(TARGET) test_transp

.PHONY: all clean test
