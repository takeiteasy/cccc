/* Companion header for tests/test_native_long_long_1234.c.
 *
 * Spells both the return type and the parameter `long long` on purpose --
 * this file is compiled as ordinary C alongside the -c=native output (its
 * `#include` is replayed verbatim), so a re-emitted prototype that spelled
 * the type `long` instead would be a "conflicting types" error. */
#ifndef CCCC_TEST_NATIVE_LONG_LONG_1234_H
#define CCCC_TEST_NATIVE_LONG_LONG_1234_H

long long ll_triple(long long v);

#endif
