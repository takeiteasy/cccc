// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: call to undeclared function 'isalpha_l'
// CCCC_EXPECT_STDERR: ISO C99 and later do not support implicit function
// declarations
//
// #1144: CCCC used to silently accept a call to a registered FFI cfunc
// (isalpha_l, src/stdlib/ctype.c) with no declaration anywhere in scope --
// no #include <ctype.h>, just the unrelated <locale.h> for locale_t/
// newlocale(). The VM tolerated it (an implicit call resolves against the
// FFI table purely at codegen, with no declaration needed at all), but
// -c=native never emits a prototype for the guessed implicit signature, so
// the generated C failed with "use of undeclared identifier" on both
// macOS and Linux. This is the ticket's own repro: at CCCC's C23 default
// std, implicit function declaration is now a hard parser error, matching
// what every real host C compiler has done since C99 -- so the failure now
// surfaces at the source of the divergence instead of downstream in the
// native backend.
#include <locale.h>

int main(void) {
    locale_t l = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    int      r = isalpha_l('a', l);
    return r ? 42 : 0;
}
