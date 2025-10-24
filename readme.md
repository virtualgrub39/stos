# STOS

Configurable, static-memory-only, platform-independent FORTH interpreter written in C

## Description

**STOS** is a bytecode-oriented FORTH interpreter, built for extreme compile-time configurability and small hardware-software interface requirements.
The main premise is ability to fine-tune RAM usage, by changing macro values (in **stos.h**). This way you can achieve complete FORTH experience with as little as 2kB of RAM. All of this makes STOS perfect for ultra-low-power embedded platforms, like AVR and PIC MCUs.

**STOS** tries to loosely implement **ANS FORTH** standard, though it's not completed at the moment. That being said, the point of **STOS** is not to implement any particular standard, but to let the user mix and match the primitives that they need on the particular platform / for their usecase.

## Platform requirements

- Implementation of three functions (declared in **stos.h**):
```c
void stos_preinit (void);   // runs once, before any IO (use this for any platform-dependent initialization code)
char stos_getc (void);      // get character from user, blocking
void stos_putc (char c);    // display character to the user
```
- At least 12kB of FLASH (it compiles to 10370 bytes with mp-lab xc8 for avr16dd14, including the platform-dependent code);
- At least 2kb of RAM (You can push it down more, but You'll have to sacrifice some features);

## Compilation and flashing

**STOS** is a complete program - it includes `main` function. The only requirement is for you to implement io functions for your platform (for example, using UART).

That being said, **STOS** is meant to be edited to your needs / to fit your platform. Many things can be changed from the **stos.h** header, but if your platform is esoteric enough, you'll probably have to make changes to the **stos.c** itself.

## Implemented words

See `stos_register_primitives` function in `stos.c` :)

All primitives should work as described in **ANS FORTH**.

WARNING: `s"` works using a single static buffer, with a very primitive bump-allocator. This means, that you can define multiple strings, and do some
cursed things with them, and be just fine in most cases, but it can break very easily if you do to much. Ex:
```
STOS>> s" very long"
STOS>> s" xD"
STOS>> >r rot rot r> rot rot
STOS>> type cr
very long
STOS>> s" another"
STOS>> >r rot rot r> rot rot
STOS>> type cr
^@D                <-- CORRUPTED
STOS>> type cr
another
```

# License

STOS is licensed under [GPL3](https://www.gnu.org/licenses/gpl-3.0.txt) - see [LICENSE](LICENSE).

Fuck corporations
