// CCCC_FLAGS: --format-string-checks
// CCCC_EXPECT_STDERR: expected type 'char \*'
// CCCC_EXPECT_STDERR: expected type 'short \*'
// CCCC_EXPECT_STDERR: expected type 'long \*'
// CCCC_EXPECT_STDERR: expected type 'unsigned long \*'
/*
 * #1230: the -F format checker's `case 'n'` arm hardcoded FMT_EXPECT_INT_PTR
 * for every length-modifier spelling, so `%ln` accepted `int *` and `%hhn`
 * accepted `int *`. It now mirrors the modifier-aware `%d`/`%u` arms:
 * %hhn -> char *, %hn -> short *, %ln/%lln/%Ln/%jn/%tn -> long *,
 * %zn -> unsigned long *. Bare `%n` stays lenient. Both the printf and scanf
 * sides of the checker are updated; this file exercises the printf side.
 */

#include "stdio.h"

int main(void) {
    int  i = 0;
    long l = 0;

    // Each of these passes a pointer wider or narrower than the modifier
    // wants, so the checker must complain.
    printf("%hhn", &i); // wants char *
    printf("%hn", &i);  // wants short *
    printf("%ln", &i);  // wants long *
    printf("%lln", &i); // wants long * (checker doesn't split long/long long)
    printf("%Ln", &i);  // wants long * (GNU L == ll, #1228 rule)
    printf("%zn", &i);  // wants unsigned long *

    // Positive controls: these are well-typed and must NOT warn.
    printf("%ln", &l);
    printf("%n", &i);

    return 42;
}
