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

#ifndef STOS_H
#define STOS_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t stos_size_t;
typedef int16_t stos_ssize_t;
typedef int16_t stos_number_t;
typedef uintptr_t stos_cell_t;
#define SIZEOF_OPCODE 1

// all types have to fit in stos_cell_t
_Static_assert (sizeof (stos_size_t) <= sizeof (stos_cell_t), "stos_size_t too large for stos_cell_t");
_Static_assert (sizeof (stos_ssize_t) <= sizeof (stos_cell_t), "stos_ssize_t too large for stos_cell_t");
_Static_assert (sizeof (stos_number_t) <= sizeof (stos_cell_t), "stos_number_t too large for stos_cell_t");

#ifdef _STOS_NO_STDBOOL
typedef uint8_t bool;
#define true 1
#define false 0
#else
#include <stdbool.h>
#endif

#define INPUT_ACCUMULATOR_LEN 128
#define DATA_STACK_SIZE 128
#define BYTECODE_SIZE 1024
#define VARSPACE_SIZE 64
#define STRINGSPACE_SIZE 16
#define MAX_WORDS 256 // including primitives, variables and constants
#define MAX_PRIMITIVES 64
#define RETURN_STACK_SIZE 64
#define COMPILE_STACK_SIZE 32
#define MAX_STRING_SIZE 12

_Static_assert (MAX_PRIMITIVES <= MAX_WORDS, "primitives can't fit into words");

// word flags
#define STOS_PRIMITIVE 1
#define STOS_IMMEDIATE 2

typedef bool (*stos_primitive_fn) (void);

void stos_preinit (void);
char stos_getc (void);
void stos_putc (char c);

#endif
