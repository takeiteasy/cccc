// CCCC_FLAGS: --format-string-checks
// CCCC_EXPECT_STDERR: expected type 'char \*'
// CCCC_EXPECT_STDERR: expected type 'short \*'
// CCCC_EXPECT_STDERR: expected type 'long \*'
// CCCC_REJECT_STDERR: expected type 'unsigned long \*'
/*
 * #1230: the -F format checker's `case 'n'` arm hardcoded FMT_EXPECT_INT_PTR
 * for every length-modifier spelling, so `%ln` accepted `int *` and `%hhn`
 * accepted `int *`. It now mirrors the modifier-aware `%d`/`%u` arms -- with
 * one difference: `%n` has no signed/unsigned conversion split, so the wide
 * modifiers accept an 8-byte target of *either* signedness (`long *` or
 * `unsigned long *`), rejecting only a narrower pointer. That matches
 * gcc/clang `-Wformat`, which check `%n`'s argument for width only. The
 * CCCC_REJECT_STDERR line pins that: a well-typed `unsigned long *` passed
 * to `%ln` must never produce the "expected type 'unsigned long *'"
 * diagnostic the pre-#1230 %zn mapping would have.
 *
 * Both the printf and scanf sides of the checker are updated; this file
 * exercises the printf side.
 */

#include "stdio.h"

int main(void) {
    int  i = 0;
    long l = 0;

    // Narrower pointer than the modifier wants -> the checker must complain.
    printf("%hhn", &i); // wants char *
    printf("%hn", &i);  // wants short *
    printf("%ln", &i);  // wants long *
    printf("%lln", &i); // wants long * (checker doesn't split long/long long)
    printf("%Ln", &i);  // wants long * (GNU L == ll, #1228 rule)
    printf("%jn", &i);  // wants long *
    printf("%zn", &i);  // wants long * (sign-agnostic 8-byte)
    printf("%tn", &i);  // wants long *

    // Positive controls: well-typed, must NOT warn.
    unsigned long ul = 0;
    printf("%ln", &l);  // signed long target
    printf("%ln", &ul); // unsigned long target -- also fine for %n
    printf("%zn", &ul); // size_t-ish target
    printf("%n", &i);   // bare %n stays lenient

    return 42;
}
