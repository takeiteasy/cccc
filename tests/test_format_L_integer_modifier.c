// CCCC_FLAGS: --format-string-checks
// CCCC_EXPECT_STDERR: expected type 'long'
/*
 * #1228: `L` on an integer conversion (%Ld/%Li/%Lu/%Lo/%Lx) is the GNU
 * pre-C99 spelling of `ll` and the -F checker treats it as `long` /
 * `unsigned long`, matching gcc/clang -Wformat.
 *
 * Kept in its own file rather than folded into
 * test_format_length_modifier_invalid.c on purpose: that file already
 * asserts every relevant type-name string ('long', 'unsigned long',
 * 'long double') appears in stderr, so an added %Ld case there could not
 * distinguish the fixed behaviour from the pre-fix one (where `%Ld` was
 * parsed as `long double`). Here the sole diagnostic is the %Ld/double
 * mismatch: before the fix `%Ld` expected `long double`, a `double`
 * argument matched, and nothing was emitted.
 */

#include "stdio.h"

int main() {
    // %Ld expects long; a double argument is incompatible.
    printf("%Ld\n", 3.14);

    // %Lx expects unsigned long; a double argument is incompatible.
    printf("%Lx\n", 3.14);

    return 42;
}
