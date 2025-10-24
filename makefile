CFLAGS = -Wall -Wextra -Werror
CFLAGS = -std=c11
CFLAGS += -ggdb
CFLAGS += -D_STOS_INTERACTIVE
# LDFLAGS = -Wl,-Map=firmware.map -Wl,--gc-sections
LDFLAGS = -lncurses

stos-unix: stos.c io.curses.c
	$(CC) -o $@ $(CFLAGS) $^ $(LDFLAGS) 
