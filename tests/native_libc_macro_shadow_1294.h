/* Companion header for tests/test_native_libc_macro_shadow_1294.c.
 *
 * Pulls in <stdio.h>/<stdarg.h> itself, rather than the test file doing so
 * directly -- this is what makes the #include uncaptured (auto-capture only
 * fires for a directive written in a command-line input or a cccc-only
 * header, see src/preprocess.c's ac_include_line block), which is what
 * triggers the #1096 fallback prototype this test exercises in the first
 * place. A direct `#include <stdio.h>` in the primary file would be
 * captured and the fallback would never fire, defeating the point of the
 * test. */
#ifndef CCCC_TEST_NATIVE_LIBC_MACRO_SHADOW_1294_H
#define CCCC_TEST_NATIVE_LIBC_MACRO_SHADOW_1294_H

#include <stdarg.h>
#include <stdio.h>

#endif
