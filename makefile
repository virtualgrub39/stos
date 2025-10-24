CFLAGS = -Wall -Wextra -Werror
CFLAGS = -std=c99
CFLAGS += -ggdb
CFLAGS += -D_STOS_INTERACTIVE
# LDFLAGS = -Wl,-Map=firmware.map -Wl,--gc-sections

stos-unix: stos.c io.libc.c
	$(CC) -o $@ $(CFLAGS) $^ $(LDFLAGS) 
