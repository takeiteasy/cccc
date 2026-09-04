/* Companion header for tests/test_native_volatile_global_1291.c.
 *
 * Spells both globals `volatile` on purpose -- this file's `#include` is
 * replayed verbatim into -c=native's generated C, so a re-emitted
 * declaration/definition that dropped the qualifier would collide with this
 * header's own ("redeclaration/redefinition ... with a different type"). */
#ifndef CCCC_TEST_NATIVE_VOLATILE_GLOBAL_1291_H
#define CCCC_TEST_NATIVE_VOLATILE_GLOBAL_1291_H

extern volatile int g_vol_scalar;
extern volatile int g_vol_array[4];

#endif
