// CCCC_FLAGS: --std=c89 -Wimplicit-function-declaration
// CCCC_EXPECT_STDERR: implicit declaration of function 'isalpha_l'
//
// #1144: same repro as test_implicit_function_error_1144.c (a call to the
// registered isalpha_l FFI cfunc with only <locale.h> in scope, no
// <ctype.h>), but at --std=c89/gnu89 implicit function declaration stays
// what it always was in that standard: a warning, not an error, and the
// call still resolves at runtime through the FFI table exactly as before.
#include <locale.h>

int main(void) {
    locale_t l = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    int      r = isalpha_l('a', l);
    return r ? 42 : 0;
}
