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

const char *stos_errstr = NULL;

void
stos_seterrstr (const char *msg)
{
    stos_errstr = msg;
}

char stos_input[INPUT_ACCUMULATOR_LEN];
char *stos_input_cursor = NULL;

enum token_type
{
    TOKEN_WORD,
    TOKEN_NUMBER,
    TOKEN_EOEXPR,
    TOKEN_REBOOT,
};

struct token
{
    enum token_type type;
    union
    {
        stos_number_t number;
        char *str;
    };
} current_token;

stos_primitive_fn stos_prims[MAX_PRIMITIVES];
stos_size_t stos_prim_count = 0;

enum stos_opcode
{
    OPCODE_PUSH_CELL,
    OPCODE_PUSH_STRING,
    OPCODE_CALL_ID,
    OPCODE_JMP,
    OPCODE_JZ,
    OPCODE_JNZ,
    OPCODE_RET,
    OPCODE_DO,
    OPCODE_LOOP,
    OPCODE_PRINT_STR,
};

struct stos_word
{
    char name[MAX_STRING_SIZE];
    stos_size_t code_off, code_len;
    uint8_t flags;
} stos_words[MAX_WORDS];
stos_size_t stos_word_count = 0;

enum stos_mode
{
    MODE_INTERPRET,
    MODE_COMPILE_NAME,
    MODE_COMPILE_TOKS,
} stos_mode
    = MODE_INTERPRET,
    stos_mode_prev = MODE_INTERPRET;

void
stos_mode_set (enum stos_mode mode)
{
    stos_mode_prev = stos_mode;
    stos_mode = mode;
}

void
stos_mode_revert (void)
{
    stos_mode = stos_mode_prev;
}

uint8_t stos_bytecode[BYTECODE_SIZE];
stos_size_t stos_pc = 0;

stos_cell_t stos_dstack[DATA_STACK_SIZE];
stos_size_t stos_dsp = 0;

bool
stos_push (stos_cell_t n)
{
    if (stos_dsp >= DATA_STACK_SIZE)
    {
        stos_seterrstr ("DATA STACK OVERFLOW");
        return false;
    }

    stos_dstack[stos_dsp++] = n;
    return true;
}

bool
stos_pop (stos_cell_t *n)
{
    if (stos_dsp == 0)
    {
        stos_seterrstr ("DATA STACK UNDERFLOW");
        return false;
    }

    *n = stos_dstack[--stos_dsp];
    return true;
}

stos_size_t stos_rstack[RETURN_STACK_SIZE];
stos_size_t stos_rsp;

bool
stos_rpush (stos_size_t n)
{
    if (stos_rsp >= RETURN_STACK_SIZE)
    {
        stos_seterrstr ("RETURN STACK OVERFLOW");
        return false;
    }

    stos_rstack[stos_rsp++] = n;
    return true;
}

bool
stos_rpop (stos_size_t *n)
{
    if (stos_rsp == 0)
    {
        stos_seterrstr ("RETURN STACK UNDERFLOW");
        return false;
    }

    *n = stos_rstack[--stos_rsp];
    return true;
}

stos_size_t stos_cstack[COMPILE_STACK_SIZE];
stos_size_t stos_csp;

bool
stos_cpush (stos_size_t n)
{
    if (stos_csp >= COMPILE_STACK_SIZE)
    {
        stos_seterrstr ("COMPILE STACK OVERFLOW");
        return false;
    }

    stos_cstack[stos_csp++] = n;
    return true;
}

bool
stos_cpop (stos_size_t *n)
{
    if (stos_csp == 0)
    {
        stos_seterrstr ("COMPILE STACK UNDERFLOW");
        return false;
    }

    *n = stos_cstack[--stos_csp];
    return true;
}

uint8_t stos_varspace[VARSPACE_SIZE];
stos_size_t stos_vsp = 0;

char stos_string[STRINGSPACE_SIZE];
stos_size_t stos_strp = 0;

void
stos_input_clear (void)
{
    stos_input[0] = 0;
    stos_input_cursor = NULL;
}

static inline bool
stos_isspace (char c)
{
    unsigned char uc = (unsigned char)c;
    return (uc == ' ' || uc == '\t' || uc == '\n' || uc == '\r' || uc == '\f' || uc == '\v');
}

static inline bool
stos_isdigit (char c)
{
    unsigned char uc = (unsigned char)c;
    return uc >= '0' && uc <= '9';
}

void *
stos_memcpy (void *dest, const void *src, stos_size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
    {
        *d++ = *s++;
    }
    return dest;
}

void *
stos_memmove (void *dest, const void *src, stos_size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0)
        return dest;
    if (d < s)
    {
        while (n--)
            *d++ = *s++;
    }
    else
    {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dest;
}

stos_size_t
stos_strlen (const char *str)
{
    const char *s = str;
    stos_size_t len = 0;
    if (!s)
        return 0;
    while (*s++)
        ++len;
    return len;
}

stos_number_t
stos_aton (const char *str, char **endptr)
{
    const char *p = str;
    stos_number_t result = 0;
    bool negative = false;

    while (stos_isspace (*p))
        p++;

    if (*p == '-')
    {
        negative = true;
        p++;
    }
    else if (*p == '+')
    {
        p++;
    }

    if (!stos_isdigit (*p))
    {
        if (endptr)
            *endptr = (char *)str;
        return 0;
    }

    while (stos_isdigit (*p))
    {
        stos_number_t digit = *p - '0';
        result = result * 10 + digit;
        p++;
    }

    if (endptr)
        *endptr = (char *)p;

    return negative ? -result : result;
}

static inline char
stos_toupper (char c)
{
    unsigned char uc = (unsigned char)c;
    if (uc >= 'a' && uc <= 'z')
        return (char)(uc - ('a' - 'A'));
    return c;
}

bool
stos_strcasesame (const char *a, const char *b)
{
    if (!a || !b)
        return 0;

    while (*a && *b)
    {
        if (stos_toupper (*a) != stos_toupper (*b))
            return 0;
        ++a;
        ++b;
    }
    return (*a == *b) ? 1 : 0;
}

void
stos_write (const char *str)
{
    while (*str)
    {
        stos_putc (*str);
        str++;
    }
}

void
stos_puts (const char *str)
{
    stos_write (str);
    stos_putc ('\r');
    stos_putc ('\n');
}

void
stos_putn (stos_number_t n)
{
    char buf[8 * sizeof (stos_number_t)]; // kindof stupid tbh, but it works?
    uint8_t i = 0;
    bool neg = (n < 0);
    stos_number_t t = neg ? -n : n;

    if (t == 0)
        buf[i++] = '0';
    while (t > 0 && i < (sizeof (buf) - 1))
    {
        buf[i++] = '0' + (t % 10);
        t /= 10;
    }
    if (neg && i < (sizeof (buf) - 1))
        buf[i++] = '-';

    for (uint8_t a = 0, b = i; a < b / 2; ++a)
    {
        char tmp = buf[a];
        buf[a] = buf[b - 1 - a];
        buf[b - 1 - a] = tmp;
    }
    buf[i] = 0;

    stos_write (buf);
}

bool
stos_token_next (void)
{
    if (stos_input_cursor == NULL)
        stos_input_cursor = stos_input;

    while (stos_isspace (*stos_input_cursor))
        ++stos_input_cursor;

    if (*stos_input_cursor == '\0')
    {
        current_token.type = TOKEN_EOEXPR;
        return true;
    }

    char *p = stos_input_cursor, *q = stos_input_cursor;

    while (!stos_isspace (*q) && *q)
        q++;

    stos_input_cursor = q;
    if (*q != '\0')
        stos_input_cursor += 1;

    *q = '\0';

    if (*p == 0x04) // EOT
    {
        current_token.type = TOKEN_REBOOT;
        return true;
    }

    if (stos_isdigit (*p) || *p == '-' || *p == '+')
    {
        char *endptr = NULL;
        stos_number_t d = stos_aton (p, &endptr);
        if (*endptr == '\0') // valid number
        {
            current_token.type = TOKEN_NUMBER;
            current_token.number = (stos_number_t)d;
            return true;
        }
    }

    if (*p == '\'')
    {
        if (*(p + 1) && *(p + 2) == '\'') // char
        {
            current_token.type = TOKEN_NUMBER;
            current_token.number = (stos_number_t) * (p + 1);
            return true;
        }
    }

    current_token.type = TOKEN_WORD;
    current_token.str = p;
    return true;
}

void
stos_bc_emit_op (enum stos_opcode op)
{
    for (size_t i = 0; i < SIZEOF_OPCODE; i++)
        stos_bytecode[stos_pc++] = (uint8_t)(op >> (i * 8));
}

void
stos_bc_emit_size (stos_size_t s)
{
    for (size_t i = 0; i < sizeof (stos_size_t); i++)
        stos_bytecode[stos_pc++] = (uint8_t)(s >> (i * 8));
}

void
stos_bc_emit_addr (stos_cell_t addr)
{
    for (size_t i = 0; i < sizeof (stos_cell_t); i++)
        stos_bytecode[stos_pc++] = (uint8_t)(addr >> (i * 8));
}

// void
// stos_bc_emit_number (stos_number_t num)
// {
//     for (size_t i = 0; i < sizeof (stos_number_t); i++)
//         stos_bytecode[stos_pc++] = (uint8_t)(num >> (i * 8));
// }

// stos_number_t
// stos_bc_read_number (stos_size_t *addr)
// {
//     stos_number_t result = 0;
//     for (size_t i = 0; i < sizeof (stos_number_t); i++)
//         result |= ((stos_number_t)stos_bytecode[(*addr)++]) << (i * 8);
//     return result;
// }

stos_size_t
stos_bc_read_size (stos_size_t *addr)
{
    stos_size_t result = 0;
    for (size_t i = 0; i < sizeof (stos_size_t); i++)
        result |= ((stos_size_t)stos_bytecode[(*addr)++]) << (i * 8);
    return result;
}

stos_cell_t
stos_bc_read_addr (stos_size_t *addr)
{
    stos_cell_t result = 0;
    for (size_t i = 0; i < sizeof (stos_cell_t); i++)
        result |= ((stos_cell_t)stos_bytecode[(*addr)++]) << (i * 8);
    return result;
}

stos_ssize_t
stos_word_create (const char *name, uint8_t flags)
{
    if (stos_word_count >= MAX_WORDS)
        return -1;
    stos_size_t id = stos_word_count++;
    stos_memcpy (stos_words[id].name, name, stos_strlen (name));
    stos_words[id].code_off = stos_pc;
    stos_words[id].code_len = 0;
    stos_words[id].flags = flags;
    return id;
}

void
stos_word_finish (stos_size_t id)
{
    stos_words[id].code_len = stos_pc - stos_words[id].code_off;
}

bool
stos_primitive_compile (const char *name, stos_primitive_fn fn, uint8_t flags)
{
    if (stos_prim_count >= MAX_PRIMITIVES)
    {
        stos_seterrstr ("PRIMITIVES AT CAPACITY");
        return false;
    }

    stos_ssize_t id = stos_word_create (name, flags | STOS_PRIMITIVE);
    if (id < 0)
        return false;

    stos_prims[id] = fn;
    stos_bc_emit_op (OPCODE_CALL_ID);
    stos_bc_emit_size (id);
    stos_bc_emit_op (OPCODE_RET);
    stos_word_finish (id);
    return true;
}

bool
stos_strto_wrdid (const char *str, uint16_t *out_id)
{
    for (int i = 0; i < stos_word_count; ++i)
    {
        if (stos_strcasesame (str, stos_words[i].name))
        {
            *out_id = i;
            return true;
        }
    }
    return false;
}

bool
stos_word_exec (stos_size_t id)
{
    if (stos_words[id].flags & STOS_PRIMITIVE)
        return stos_prims[id]();
    else
    {
        stos_size_t _pc = stos_words[id].code_off;

        while (true)
        {
            uint8_t op = stos_bytecode[_pc++];
            switch (op)
            {
            case OPCODE_PUSH_CELL: {
                stos_cell_t v = stos_bc_read_addr (&_pc);
                stos_push (v);
                break;
            }
            case OPCODE_CALL_ID: {
                stos_size_t tid = stos_bc_read_size (&_pc);

                if (stos_words[tid].flags & STOS_PRIMITIVE)
                    stos_prims[tid]();
                else
                {
                    stos_rpush (_pc);
                    _pc = stos_words[tid].code_off;
                }
                break;
            }
            case OPCODE_RET: {
                if (stos_rsp == 0)
                    return true;

                if (!stos_rpop (&_pc))
                    return false;
                break;
            }
            case OPCODE_JZ: {
                stos_cell_t b;
                if (!stos_pop (&b))
                    return false;
                stos_size_t addr = stos_bc_read_size (&_pc);
                if (b == 0)
                    _pc = addr;
                break;
            }
            case OPCODE_JNZ: {
                stos_cell_t b;
                if (!stos_pop (&b))
                    return false;
                stos_size_t addr = stos_bc_read_size (&_pc);
                if (b != 0)
                    _pc = addr;
                break;
            }
            case OPCODE_JMP: {
                stos_size_t addr = stos_bc_read_size (&_pc);
                _pc = addr;
                break;
            }
            case OPCODE_DO: {
                stos_cell_t limit;
                if (!stos_pop (&limit))
                    return false;
                stos_cell_t start;
                if (!stos_pop (&start))
                    return false;

                stos_rpush ((stos_size_t)limit);
                stos_rpush ((stos_size_t)start);
                break;
            }
            case OPCODE_LOOP: {
                stos_cell_t incr;
                if (!stos_pop (&incr))
                    return false;
                stos_size_t target = stos_bc_read_size (&_pc);

                stos_size_t index = stos_rstack[stos_rsp - 1];
                index += incr;
                stos_rstack[stos_rsp - 1] = index;

                stos_size_t limit = stos_rstack[stos_rsp - 2];

                if (index < limit)
                    _pc = target;
                else
                    stos_rsp -= 2;
                break;
            }
            case OPCODE_PRINT_STR: {
                stos_size_t len = stos_bc_read_size (&_pc);
                for (stos_size_t i = 0; i < len; i++)
                    stos_putc (stos_bytecode[_pc++]);
                break;
            }
            case OPCODE_PUSH_STRING: {
                stos_size_t len = stos_bc_read_size (&_pc);

                stos_cell_t addr = (stos_cell_t)&stos_bytecode[_pc];
                // stos_push (addr);

                _pc += len;

                // stos_push ((stos_cell_t)len);

                stos_memcpy (stos_string + stos_strp, (char *)addr, len);
                stos_string[stos_strp + len] = '\0';

                if (!stos_push ((stos_cell_t)stos_string + stos_strp))
                    return false;

                stos_strp += len + 1;

                return stos_push ((stos_cell_t)len);
            }
            }
        }

        return true;
    }
}

bool
stos_token_compile (void)
{
    switch (current_token.type)
    {
    case TOKEN_WORD: {
        uint16_t wid;
        if (!stos_strto_wrdid (current_token.str, &wid))
        {
            stos_seterrstr ("INVALID WORD");
            return false;
        }
        else if ((stos_words[wid].flags & STOS_IMMEDIATE) && (stos_words[wid].flags & STOS_PRIMITIVE))
            return stos_prims[wid]();
        else
        {
            stos_bc_emit_op (OPCODE_CALL_ID);
            stos_bc_emit_size (wid);
            return true;
        }
        break;
    }
    case TOKEN_NUMBER: {
        stos_bc_emit_op (OPCODE_PUSH_CELL);
        stos_bc_emit_addr (current_token.number);
        break;
    }
    default:
        break;
    }

    return true;
}

bool
prim_dot (void)
{
    stos_cell_t n;
    if (!stos_pop (&n))
        return false;
    stos_putn (n);
    stos_putc (' ');
    return true;
}

bool
prim_plus (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a) || !stos_pop (&b))
        return false;
    return stos_push (a + b);
}

bool
prim_def (void)
{
    stos_mode_set (MODE_COMPILE_NAME);
    return true;
}

bool
prim_enddef (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("END OF DEFINITION OUTSIDE OF DEFINITION");
        return false;
    }

    stos_bc_emit_op (OPCODE_RET);
    stos_word_finish (stos_word_count - 1);
    stos_mode_set (MODE_INTERPRET);
    return true;
}

bool
prim_words (void)
{
    for (int i = 0; i < stos_word_count; ++i)
    {
        stos_write (stos_words[i].name);
        stos_putc (' ');
    }
    stos_putc ('\r');
    stos_putc ('\n');
    return true;
}

bool
prim_swap (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a))
        return false;
    if (!stos_pop (&b))
        return false;
    if (!stos_push (a))
        return false;
    return stos_push (b);
}

bool
prim_over (void)
{
    stos_cell_t v = stos_dstack[stos_dsp - 2];
    return stos_push (v);
}

bool
prim_drop (void)
{
    stos_cell_t a;
    return stos_pop (&a);
}

bool
prim_dup (void)
{
    stos_cell_t v = stos_dstack[stos_dsp - 1];
    return stos_push (v);
}

bool
prim_exit (void)
{
    stos_bc_emit_op (OPCODE_RET);
    return true;
}

bool
prim_minus (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a))
        return false;
    if (!stos_pop (&b))
        return false;
    return stos_push (b - a);
}

bool
prim_eq (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a))
        return false;
    if (!stos_pop (&b))
        return false;
    return stos_push (a == b ? -1 : 0);
}

bool
prim_if (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`IF` OUTSIDE OF DEFINITION");
        return false;
    }

    stos_bc_emit_op (OPCODE_JZ);
    stos_cpush (stos_pc);
    stos_bc_emit_size (0); // placeholder
    return true;
}

bool
prim_else (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`ELSE` OUTSIDE OF DEFINITION");
        return false;
    }

    stos_size_t if_addr;
    if (!stos_cpop (&if_addr))
        return false;

    stos_bc_emit_op (OPCODE_JMP);
    stos_cpush (stos_pc);
    stos_bc_emit_size (0); // placeholder

    for (size_t i = 0; i < sizeof (stos_size_t); i++)
        stos_bytecode[if_addr + i] = (uint8_t)(stos_pc >> (i * 8));
    return true;
}

bool
prim_endif (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`THEN` OUTSIDE OF DEFINITION");
        return false;
    }

    stos_size_t addr;
    if (!stos_cpop (&addr))
        return false;

    for (size_t i = 0; i < sizeof (stos_size_t); i++)
        stos_bytecode[addr + i] = (uint8_t)(stos_pc >> (i * 8));
    return true;
}

bool
prim_do (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`DO` OUTSIDE OF DEFINITION");
        return false;
    }
    stos_bc_emit_op (OPCODE_DO);
    return stos_cpush (stos_pc);
}

bool
prim_loop (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`LOOP` OUTSIDE OF DEFINITION");
        return false;
    }
    stos_bc_emit_op (OPCODE_PUSH_CELL);
    stos_bc_emit_addr (1);
    stos_bc_emit_op (OPCODE_LOOP);
    stos_size_t addr;
    if (!stos_cpop (&addr))
        return false;
    stos_bc_emit_size (addr);
    return true;
}

bool
prim_ploop (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`+LOOP` OUTSIDE OF DEFINITION");
        return false;
    }
    stos_bc_emit_op (OPCODE_LOOP);
    stos_size_t addr;
    if (!stos_cpop (&addr))
        return false;
    stos_bc_emit_size (addr);
    return true;
}

bool
prim_tor (void)
{
    stos_cell_t v;
    if (!stos_pop (&v))
        return false;

    return stos_rpush (v);
}

bool
prim_fromr (void)
{
    stos_size_t v;
    if (!stos_rpop (&v))
        return false;
    return stos_push ((stos_number_t)v);
}

bool
prim_rfetch (void)
{
    stos_size_t v = stos_rstack[stos_rsp - 1];
    stos_push ((stos_number_t)v);
    return true;
}

bool
prim_rot (void)
{
    stos_cell_t a, b, c;
    if (!stos_pop (&c))
        return false;
    if (!stos_pop (&b))
        return false;
    if (!stos_pop (&a))
        return false;

    if (!stos_push (b))
        return false;
    if (!stos_push (c))
        return false;
    return stos_push (a);
}

bool
prim_putstack (void)
{
    stos_putc ('<');
    stos_putn (stos_dsp);
    stos_putc ('>');
    stos_putc (' ');
    for (stos_size_t i = 0; i < stos_dsp; ++i)
    {
        stos_putn (stos_dstack[i]);
        stos_putc (' ');
    }
    stos_putc ('\r');
    stos_putc ('\n');
    return true;
}

bool
prim_emit (void)
{
    stos_cell_t c;
    if (!stos_pop (&c))
        return false;
    stos_putc (c);
    return true;
}

bool
prim_mult (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a))
        return false;
    if (!stos_pop (&b))
        return false;
    return stos_push (a * b);
}

bool
prim_div (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a))
        return false;
    if (!stos_pop (&b))
        return false;
    return stos_push (b / a);
}

bool
prim_mod (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a))
        return false;
    if (!stos_pop (&b))
        return false;
    return stos_push (b % a);
}

bool
prim_lt (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a))
        return false;
    if (!stos_pop (&b))
        return false;
    return stos_push (b < a);
}

bool
prim_lte (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a))
        return false;
    if (!stos_pop (&b))
        return false;
    return stos_push (b <= a);
}

bool
prim_gt (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a))
        return false;
    if (!stos_pop (&b))
        return false;
    return stos_push (b > a);
}

bool
prim_gte (void)
{
    stos_cell_t a, b;
    if (!stos_pop (&a))
        return false;
    if (!stos_pop (&b))
        return false;
    return stos_push (b >= a);
}

bool
prim_fetch (void)
{
    stos_cell_t addr;
    if (!stos_pop (&addr))
        return false;

    stos_cell_t value = *(stos_cell_t *)addr;
    return stos_push (value);
}

bool
prim_store (void)
{
    stos_cell_t addr, value;
    if (!stos_pop (&addr))
        return false;
    if (!stos_pop (&value))
        return false;

    *(stos_cell_t *)addr = value;
    return true;
}

bool
prim_cfetch (void)
{
    stos_cell_t addr;
    if (!stos_pop (&addr))
        return false;

    uint8_t value = *(uint8_t *)addr;
    return stos_push (value);
}

bool
prim_cstore (void)
{
    stos_cell_t addr, value;
    if (!stos_pop (&addr))
        return false;
    if (!stos_pop (&value))
        return false;

    *(uint8_t *)addr = (uint8_t)value;
    return true;
}

bool
prim_key (void)
{
    stos_cell_t c = stos_getc ();
    // fprintf (stderr, "[c = 2x%02X; %c]\n", (char)c, (char)c);
    return stos_push (c);
}

bool
prim_begin (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`BEGIN` OUTSIDE OF DEFINITION");
        return false;
    }
    return stos_cpush (stos_pc);
}

bool
prim_until (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`UNTIL` OUTSIDE OF DEFINITION");
        return false;
    }

    stos_size_t begin;
    if (!stos_cpop (&begin))
        return false;

    stos_bc_emit_op (OPCODE_JZ);
    stos_bc_emit_size (begin);
    return true;
}

bool
prim_while (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`WHILE` OUTSIDE OF DEFINITION");
        return false;
    }

    if (!stos_cpush (stos_pc))
        return false;

    stos_bc_emit_op (OPCODE_JZ);
    stos_bc_emit_size (0); // placeholder
    return true;
}

bool
prim_repeat (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`REPEAT` OUTSIDE OF DEFINITION");
        return false;
    }

    stos_size_t while_addr, begin_addr;
    if (!stos_cpop (&while_addr))
        return false;
    if (!stos_cpop (&begin_addr))
        return false;

    stos_bc_emit_op (OPCODE_JMP);
    stos_bc_emit_size (begin_addr);

    for (size_t i = 0; i < sizeof (stos_size_t); i++)
        stos_bytecode[while_addr + i] = (uint8_t)(stos_pc >> (i * 8));
    return true;
}

bool
prim_again (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`AGAIN` OUTSIDE OF DEFINITION");
        return false;
    }

    stos_size_t loop_start;
    if (!stos_cpop (&loop_start))
        return false;

    stos_bc_emit_op (OPCODE_JMP);
    stos_bc_emit_size (loop_start);

    return true;
}

bool
prim_recurse (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`RECURSE` OUTSIDE OF DEFINITION");
        return false;
    }

    stos_size_t current_word_id = stos_word_count - 1;
    stos_bc_emit_op (OPCODE_CALL_ID);
    stos_bc_emit_size (current_word_id);
    return true;
}

bool
prim_putstr (void)
{
    if (stos_mode != MODE_COMPILE_TOKS)
    {
        stos_seterrstr ("`.\"` OUTSIDE OF DEFINITION");
        return false;
    }

    char *p = stos_input_cursor;
    while (*stos_input_cursor != '"' && *stos_input_cursor != '\0')
        stos_input_cursor++;

    if (*stos_input_cursor != '"')
    {
        stos_seterrstr ("UNTERMINATED STRING");
        return false;
    }

    stos_size_t len = stos_input_cursor - p;
    stos_input_cursor++;

    stos_bc_emit_op (OPCODE_PRINT_STR);
    stos_bc_emit_size (len);
    for (stos_size_t i = 0; i < len; i++)
        stos_bytecode[stos_pc++] = p[i];

    return true;
}

bool
prim_cr (void)
{
    stos_write ("\r\n");
    return true;
}

bool
prim_var (void)
{
    if (stos_mode != MODE_INTERPRET)
    {
        stos_seterrstr ("`VARIABLE` IN DEFINITION");
        return false;
    }

    if (!stos_token_next () || current_token.type != TOKEN_WORD)
    {
        stos_seterrstr ("EXPECTED WORD AFTER `VARIABLE`");
        return false;
    }

    if (stos_vsp >= VARSPACE_SIZE)
    {
        stos_seterrstr ("VARIABLE SPACE AT CAPACITY");
        return false;
    }

    stos_cell_t var_addr = (stos_cell_t)&stos_varspace[stos_vsp];
    stos_vsp++;

    stos_varspace[stos_vsp - 1] = 0;

    stos_ssize_t id = stos_word_create (current_token.str, 0);
    if (id < 0)
        return false;

    stos_bc_emit_op (OPCODE_PUSH_CELL);
    stos_bc_emit_addr (var_addr);
    stos_bc_emit_op (OPCODE_RET);
    stos_word_finish (id);

    return true;
}

bool
prim_constant (void)
{
    if (stos_mode != MODE_INTERPRET)
    {
        stos_seterrstr ("`CONSTANT` IN DEFINITION");
        return false;
    }

    stos_cell_t value;
    if (!stos_pop (&value))
        return false;

    if (!stos_token_next () || current_token.type != TOKEN_WORD)
    {
        stos_seterrstr ("EXPECTED WORD AFTER `CONSTANT`");
        return false;
    }

    stos_ssize_t id = stos_word_create (current_token.str, 0);
    if (id < 0)
        return false;

    stos_bc_emit_op (OPCODE_PUSH_CELL);
    stos_bc_emit_addr (value);
    stos_bc_emit_op (OPCODE_RET);
    stos_word_finish (id);

    return true;
}

bool
prim_create (void)
{
    if (stos_mode != MODE_INTERPRET)
    {
        stos_seterrstr ("`CREATE` IN DEFINITION");
        return false;
    }

    if (!stos_token_next () || current_token.type != TOKEN_WORD)
        return false;

    stos_cell_t addr = (stos_cell_t)&stos_varspace[stos_vsp];

    stos_ssize_t id = stos_word_create (current_token.str, 0);
    if (id < 0)
        return false;

    stos_bc_emit_op (OPCODE_PUSH_CELL);
    stos_bc_emit_addr (addr);
    stos_bc_emit_op (OPCODE_RET);
    stos_word_finish (id);

    return true;
}

bool
prim_allot (void)
{
    if (stos_mode != MODE_INTERPRET)
    {
        stos_seterrstr ("`ALLOT` IN DEFINITION");
        return false;
    }

    stos_cell_t n;
    if (!stos_pop (&n))
        return false;

    if (stos_vsp + n > VARSPACE_SIZE)
    {
        stos_seterrstr ("VARIABLE SPACE AT CAPACITY");
        return false;
    }

    stos_vsp += n;
    return true;
}

bool
prim_squote (void)
{
    if (stos_input_cursor == NULL)
        stos_input_cursor = stos_input;

    while (stos_isspace (*stos_input_cursor))
        stos_input_cursor++;

    char *str_start = stos_input_cursor;
    stos_size_t len = 0;

    while (*stos_input_cursor != '"' && *stos_input_cursor != '\0')
    {
        len++;
        stos_input_cursor++;
    }

    if (*stos_input_cursor != '"')
    {
        stos_seterrstr ("UNTERMINATED STRING");
        return false;
    }

    stos_input_cursor++;

    if (stos_mode == MODE_INTERPRET)
    {
        if (stos_strp + len + 1 >= STRINGSPACE_SIZE)
        {
            stos_seterrstr ("STRING TOO LONG");
            return false;
        }

        stos_memcpy (stos_string + stos_strp, str_start, len);
        stos_string[stos_strp + len] = '\0';

        if (!stos_push ((stos_cell_t)stos_string + stos_strp))
            return false;

        stos_strp += len + 1;

        return stos_push ((stos_cell_t)len);
    }
    else if (stos_mode == MODE_COMPILE_TOKS)
    {
        stos_bc_emit_op (OPCODE_PUSH_STRING);
        stos_bc_emit_size (len);

        for (stos_size_t i = 0; i < len; i++)
        {
            stos_bytecode[stos_pc++] = str_start[i];
        }

        return true;
    }

    return false;
}

bool
prim_type (void)
{
    stos_cell_t len, addr;
    if (!stos_pop (&len) || !stos_pop (&addr))
        return false;

    char *str = (char *)addr;
    for (stos_size_t i = 0; i < len; i++)
        stos_putc (str[i]);

    if (stos_strp >= len + 1)
        stos_strp -= (len + 1);
    else
        stos_strp = 0;

    return true;
}

bool
stos_register_primitives (void)
{
    bool r = !stos_primitive_compile (".", prim_dot, 0) ||                        //
             !stos_primitive_compile (".s", prim_putstack, 0) ||                  //
             !stos_primitive_compile (".\"", prim_putstr, STOS_IMMEDIATE) ||      //
             !stos_primitive_compile ("type", prim_type, STOS_IMMEDIATE) ||       //
             !stos_primitive_compile ("cr", prim_cr, 0) ||                        //
             !stos_primitive_compile ("emit", prim_emit, 0) ||                    //
             !stos_primitive_compile ("key", prim_key, 0) ||                      //
             !stos_primitive_compile ("dup", prim_dup, 0) ||                      //
             !stos_primitive_compile ("swap", prim_swap, 0) ||                    //
             !stos_primitive_compile ("over", prim_over, 0) ||                    //
             !stos_primitive_compile ("drop", prim_drop, 0) ||                    //
             !stos_primitive_compile ("rot", prim_rot, 0) ||                      //
             !stos_primitive_compile ("+", prim_plus, 0) ||                       //
             !stos_primitive_compile ("-", prim_minus, 0) ||                      //
             !stos_primitive_compile ("*", prim_mult, 0) ||                       //
             !stos_primitive_compile ("/", prim_div, 0) ||                        //
             !stos_primitive_compile ("mod", prim_mod, 0) ||                      //
             !stos_primitive_compile ("=", prim_eq, 0) ||                         //
             !stos_primitive_compile ("<", prim_lt, 0) ||                         //
             !stos_primitive_compile ("<=", prim_lte, 0) ||                       //
             !stos_primitive_compile (">", prim_gt, 0) ||                         //
             !stos_primitive_compile (">=", prim_gte, 0) ||                       //
             !stos_primitive_compile (":", prim_def, 0) ||                        //
             !stos_primitive_compile (";", prim_enddef, STOS_IMMEDIATE) ||        //
             !stos_primitive_compile ("if", prim_if, STOS_IMMEDIATE) ||           //
             !stos_primitive_compile ("else", prim_else, STOS_IMMEDIATE) ||       //
             !stos_primitive_compile ("then", prim_endif, STOS_IMMEDIATE) ||      //
             !stos_primitive_compile ("do", prim_do, STOS_IMMEDIATE) ||           //
             !stos_primitive_compile ("begin", prim_begin, STOS_IMMEDIATE) ||     //
             !stos_primitive_compile ("until", prim_until, STOS_IMMEDIATE) ||     //
             !stos_primitive_compile ("while", prim_while, STOS_IMMEDIATE) ||     //
             !stos_primitive_compile ("repeat", prim_repeat, STOS_IMMEDIATE) ||   //
             !stos_primitive_compile ("again", prim_again, STOS_IMMEDIATE) ||     //
             !stos_primitive_compile ("loop", prim_loop, STOS_IMMEDIATE) ||       //
             !stos_primitive_compile ("+loop", prim_ploop, STOS_IMMEDIATE) ||     //
             !stos_primitive_compile ("recurse", prim_recurse, STOS_IMMEDIATE) || //
             !stos_primitive_compile ("exit", prim_exit, STOS_IMMEDIATE) ||       //
             !stos_primitive_compile ("variable", prim_var, 0) ||                 //
             !stos_primitive_compile ("constant", prim_constant, 0) ||            //
             !stos_primitive_compile ("create", prim_create, 0) ||                //
             !stos_primitive_compile ("allot", prim_allot, 0) ||                  //
             !stos_primitive_compile ("s\"", prim_squote, STOS_IMMEDIATE) ||      //
             !stos_primitive_compile (">r", prim_tor, 0) ||                       //
             !stos_primitive_compile ("r>", prim_fromr, 0) ||                     //
             !stos_primitive_compile ("r@", prim_rfetch, 0) ||                    //
             !stos_primitive_compile ("@", prim_fetch, 0) ||                      //
             !stos_primitive_compile ("!", prim_store, 0) ||                      //
             !stos_primitive_compile ("c@", prim_cfetch, 0) ||                    //
             !stos_primitive_compile ("c!", prim_cstore, 0) ||                    //
             !stos_primitive_compile ("words", prim_words, 0);                    //
    return !r;
}

const char *
stos_readline (void)
{
    stos_size_t iline = 0;

    for (;;)
    {
        if (iline == INPUT_ACCUMULATOR_LEN - 1)
        {
            stos_seterrstr ("LINE TO LONG");
            return NULL;
        }

        char c = stos_getc ();
        // fprintf(stderr, "[CHAR 0x%02X]\r\n", c);
        switch (c)
        {
        case 0 ... 2:
            break;
        case 3 ... 4: // EXT (CTRL+C), EOT (CTRL+D)
            stos_input[0] = 0x04;
            stos_input[1] = 0;
            return stos_input;
        case 5 ... 7:
            break;
        case '\r':
        case '\n':
            stos_input[iline] = 0;
            return stos_input;
        case '\b':
            if (iline > 0)
                --iline;
            break;
        case 14 ... 31:
            break;
        default:
            stos_input[iline++] = c;
            break;
        }
    }
}

bool
stos_init (void)
{
    stos_dsp = 0;
    stos_rsp = 0;
    stos_csp = 0;
    stos_word_count = 0;
    stos_prim_count = 0;
    stos_mode_set (MODE_INTERPRET);
    stos_input_clear ();
    return stos_register_primitives ();
}

bool
stos_token_exec (void)
{
    if (current_token.type == TOKEN_REBOOT)
        return stos_init ();

    switch (stos_mode)
    {
    case MODE_INTERPRET: {
        switch (current_token.type)
        {
        case TOKEN_NUMBER:
            stos_push (current_token.number);
            break;
        case TOKEN_WORD: {
            uint16_t wid;
            if (!stos_strto_wrdid (current_token.str, &wid))
            {
                stos_seterrstr ("INVALID WORD");
                return false;
            }
            return stos_word_exec (wid);
        }
        case TOKEN_EOEXPR:
        default:
            break;
        }
    }
    break;
    case MODE_COMPILE_NAME: {
        if (current_token.type != TOKEN_WORD)
        {
            stos_seterrstr ("UNEXPECTED TOKEN AFTER BEGINNING OF DEFINITION");
            return false;
        }
        stos_word_create (current_token.str, 0);
        stos_mode_set (MODE_COMPILE_TOKS);
        break;
    }
    case MODE_COMPILE_TOKS: {
        if (!stos_token_compile ())
        {
            stos_pc = stos_words[stos_word_count].code_off; // reset bytecode
            return false;
        }
        return true;
    }
    }
    return true;
}

int
main (void)
{
    stos_preinit ();

    if (!stos_init ())
    {
#ifdef _STOS_INTERACTIVE
        stos_write ("STOS FAILED TO INITIALIZE ");
        stos_puts (stos_errstr);
#endif

        while (true)
            ;
    }

#ifdef _STOS_INTERACTIVE
    stos_puts ("STOS, Copyright (C) 2025 virtualgrub39");
    stos_puts ("READY");
#endif

    while (true)
    {
#ifdef _STOS_INTERACTIVE
        if (stos_mode == MODE_INTERPRET)
            stos_write ("STOS>> ");
        else
            stos_write ("....>> ");
#endif

        const char *line = stos_readline ();
        if (!line || !*line)
        {
            stos_input_clear ();
            continue;
        }

        do
        {
            stos_token_next ();
            if (!stos_token_exec ())
            {
#ifdef _STOS_INTERACTIVE
                stos_write ("ERR. ");
                stos_puts (stos_errstr);
#endif
                stos_mode = MODE_INTERPRET;
                stos_pc = 0;
                break;
            }
        } while (current_token.type != TOKEN_EOEXPR);

        stos_input_clear ();
    }
}

/*
: fib
    dup 0 = if
        drop 0 exit
    then

    dup 1 = if
        drop 1 exit
    then
    >r
    0 1
    0 r@ do
        swap over +
    loop
    drop
    r> drop
;
*/
