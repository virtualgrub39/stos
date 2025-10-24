#include "stos.h"
#include <stdio.h>

char
stos_getc (void)
{
    return getc (stdin);
}

void
stos_putc (char c)
{
    putc (c, stdout);
}
