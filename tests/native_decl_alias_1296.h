/* Companion header for tests/test_native_decl_alias_1296.c.
 *
 * Pulls in <nl_types.h>/<sys/resource.h> itself, rather than the test file
 * doing so directly, so this program exercises the two libc functions one
 * hop removed from the primary file. A bundled header reached this way is
 * now (#1297) treated as captured -- see the companion .c file's own
 * comment for what that means for this test. */
#ifndef CCCC_TEST_NATIVE_DECL_ALIAS_1296_H
#define CCCC_TEST_NATIVE_DECL_ALIAS_1296_H

#include <nl_types.h>
#include <sys/resource.h>

#endif
