#ifndef STOS_H
#define STOS_H

#include <stddef.h>
#include <stdint.h>

typedef uint64_t stos_size_t;
typedef int64_t stos_ssize_t;
typedef int64_t stos_number_t;
#define SIZEOF_OPCODE 1

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
#define MAX_WORDS 256 // including primitives
#define MAX_PRIMITIVES 64
#define RETURN_STACK_SIZE 64
#define COMPILE_STACK_SIZE 32
#define MAX_STRING_SIZE 8

// word flags
#define STOS_PRIMITIVE 1
#define STOS_IMMEDIATE 2

typedef bool (*stos_primitive_fn) (void);

char stos_getc (void);
void stos_putc (char c);

#endif
