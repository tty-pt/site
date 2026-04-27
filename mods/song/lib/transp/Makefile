include ../../../../../mk/portable.mk

CC ?= ${cc}
CFLAGS += -g -O0 -fPIC
LDFLAGS += -shared
LDLIBS += -lqmap

TARGET = libtransp.so
SRC = transp.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c transp.h
	$(CC) $(CFLAGS) -c -o $@ $<

test_transp: test_transp.c $(TARGET)
	$(CC) $(CFLAGS) -o $@ $< -L. -ltransp -Wl,-rpath,. $(LDLIBS)

test: test_transp
	LD_LIBRARY_PATH=. ./test_transp

clean:
	rm -f $(OBJ) $(TARGET) test_transp

.PHONY: all clean test
