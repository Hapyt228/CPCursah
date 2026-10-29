CC      = gcc
CFLAGS  = -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -O2
LDLIBS  = -ludev
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:.c=.o)

usbaudit: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) usbaudit

.PHONY: clean
