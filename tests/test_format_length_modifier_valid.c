// CCCC_FLAGS: --format-string-checks
/*
 * Test format string validation - valid length modifier usage
 * All calls use the correct type for the given length modifier.
 */

#include "stdio.h"

int main() {
    long l = 42L;
    unsigned long ul = 100UL;
    long double ld = 3.14L;
    short s = 5;
    char ch = 'a';

    // Long integer variants
    printf("%ld\n",  l);     // long → ok
    printf("%lld\n", l);     // long long = long in LP64 → ok
    printf("%jd\n",  l);     // intmax_t = long → ok
    printf("%td\n",  l);     // ptrdiff_t = long → ok

    // Unsigned long variants
    printf("%lu\n",  ul);    // unsigned long → ok
    printf("%llu\n", ul);    // unsigned long long = unsigned long in LP64 → ok
    printf("%zu\n",  ul);    // size_t = unsigned long → ok
    printf("%ju\n",  ul);    // uintmax_t = unsigned long → ok

    // Long double
    printf("%Lf\n",  ld);    // long double → ok
    printf("%Le\n",  ld);    // long double → ok
    printf("%Lg\n",  ld);    // long double → ok

    // Short modifiers: short/char are promoted to int in varargs, so fine
    printf("%hd\n",  s);     // short (promoted to int) → ok
    printf("%hhd\n", ch);    // char (promoted to int) → ok

    printf("\n=== All length modifier format string tests passed ===\n");
    return 42;
}
