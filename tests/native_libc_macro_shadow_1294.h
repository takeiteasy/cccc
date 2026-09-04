/* Companion header for tests/test_native_libc_macro_shadow_1294.c.
 *
 * Pulls in <stdio.h>/<stdarg.h> itself, rather than the test file doing so
 * directly, so this program exercises the fallback-prototype spellings
 * (parenthesized declarator) through a header one hop removed from the
 * primary file -- since a bundled header reached this way is now itself
 * treated as captured (a real host declaration reaches the host compiler
 * through this header's own replayed #include), the fallback prototype no
 * longer fires here at all; this test now proves the *replayed* real-SDK
 * declaration is itself accepted by the host compiler without a
 * conflicting fallback alongside it. */
#ifndef CCCC_TEST_NATIVE_LIBC_MACRO_SHADOW_1294_H
#define CCCC_TEST_NATIVE_LIBC_MACRO_SHADOW_1294_H

#include <stdarg.h>
#include <stdio.h>

#endif
