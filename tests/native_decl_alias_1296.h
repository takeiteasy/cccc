/* Companion header for tests/test_native_decl_alias_1296.c.
 *
 * Pulls in <nl_types.h>/<sys/resource.h> itself, rather than the test file
 * doing so directly -- same reasoning as
 * native_libc_macro_shadow_1294.h's own comment: an uncaptured #include is
 * what triggers the #1096 fallback prototype this test exercises. */
#ifndef CCCC_TEST_NATIVE_DECL_ALIAS_1296_H
#define CCCC_TEST_NATIVE_DECL_ALIAS_1296_H

#include <nl_types.h>
#include <sys/resource.h>

#endif
