/* STOS - FORTH interpreter
   Copyright (C) 2025 virtualgrub39

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "stos.h"
#include <ncurses.h>

void
stos_preinit (void)
{
    initscr ();
    cbreak ();
#ifdef _STOS_ECHO
    echo ();
#else
    noecho ();
#endif
    nl ();
    keypad (stdscr, TRUE);

    nodelay (stdscr, FALSE);
    timeout (-1);

    scrollok (stdscr, TRUE);
    idlok (stdscr, TRUE);
}

char
stos_getc (void)
{
    int ch;

    nodelay (stdscr, FALSE);
    timeout (-1);

    for (;;)
    {
        ch = wgetch (stdscr);
        if (ch != ERR)
            break;
    }

    if (ch == '\r' || ch == '\n' || ch == KEY_ENTER)
    {
        int y, x;
        getyx (stdscr, y, x);
        if (y >= LINES - 1)
        {
            wscrl (stdscr, 1);
            move (LINES - 1, 0);
        }
        else
        {
            move (y + 1, 0);
        }
        wrefresh (stdscr);
        return '\n';
    }

    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8 || ch == 7)
    {
        int y, x;
        getyx (stdscr, y, x);

        if (x > 7)
        {
            move (y, x - 1);
            delch ();
            wrefresh (stdscr);
            return '\b';
        }
        else
        {
            return (char)7;
        }
    }

#ifndef _STOS_ECHO
    stos_putc (ch & 0xff);
#endif

    return (char)(ch & 0xff);
}

void
stos_putc (char c)
{
    int y, x;
    getyx (stdscr, y, x);

    if (c == '\r')
    {
        move (y, 0);
    }
    else if (c == '\n')
    {
        if (y >= LINES - 1)
        {
            wscrl (stdscr, 1);
            move (LINES - 1, 0);
        }
        else
        {
            move (y + 1, 0);
        }
    }
    else if (c == '\b')
    {
        if (x > 0)
        {
            move (y, x - 1);
            delch ();
        }
        else
        {
            beep ();
        }
    }
    else
    {
        waddch (stdscr, (unsigned char)c);
    }

    wrefresh (stdscr);
}