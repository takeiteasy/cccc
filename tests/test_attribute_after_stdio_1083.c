// Ticket #1083: CCCC's own include/Availability.h stub did
// `#define __attribute__(x)` (empty) unconditionally -- correct for CCCC's
// own preprocessing (its tokenizer parses __attribute__ as a builtin
// construct itself, never via macro expansion), but under -c=native
// (tools/tests.py --native always passes -I./include) a real host
// preprocessor -- unlike CCCC's own tokenizer -- keeps that empty macro
// live for the rest of the translation unit. Once <stdio.h> pulled in
// sys/cdefs.h -> Availability.h, every later __attribute__(...) in the
// user's own TU silently vanished -- no error, no warning -- including the
// constructor/destructor attributes #1020 taught serialize.c to emit.
//
// This file's VM run can't see the bug at all (CCCC's own tokenizer never
// goes through the stub's macro-expansion path); only its --native run
// exercises it, which is why the regression guard lives in
// tools/comptime_native_smoke.py case 115, not here -- this file exists so
// the --native suite carries the shape by name.
//
// Fixed by guarding both include/Availability.h's own CCCC-flavored body
// and include/sys/cdefs.h's Availability.h include on #ifdef __CCCC__,
// handing off to the real host Availability.h (via __has_include_next)
// otherwise.

#include <stdio.h>

int ctor_ran;
int dtor_ran;

__attribute__((constructor)) void ctor(void) {
    ctor_ran = 1;
}

__attribute__((destructor)) void dtor(void) {
    dtor_ran = 1;
}

int main(void) {
    if (!ctor_ran)
        return 1;
    return 42;
}
