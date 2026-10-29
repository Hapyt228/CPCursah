CC      = gcc
CFLAGS  = -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-cast-function-type -O2 $(shell pkg-config --cflags gtk+-3.0)
LDLIBS  = $(shell pkg-config --libs gtk+-3.0) -ludev
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:.c=.o)

usbaudit: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) usbaudit

.PHONY: clean
