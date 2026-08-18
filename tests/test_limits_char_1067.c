// Ticket #1067: include/limits.h defined CHAR_BIT/SCHAR_MIN/SCHAR_MAX/
// UCHAR_MAX and the short/int/long/long-long limits, but had no CHAR_MIN,
// no CHAR_MAX, and no MB_LEN_MAX at all -- C17 5.2.4.2.1p1 requires all
// three. Any program using the standard `char buf[MB_LEN_MAX]` idiom, or
// clamping a value to CHAR_MAX, failed to compile.
//
// #ifndef/#error below (rather than a plain #include + use) so this file
// fails to *compile* against the pre-fix header, not just fails at runtime
// -- confirmed against the pre-fix binary before landing (this batch's own
// recurring lesson: don't add a test that would pass vacuously green).
//
// This file deliberately does NOT assert a specific MB_LEN_MAX value.
// limits.h is on neither is_compiler_owned_header nor
// is_cccc_supplied_only_header (src/preprocess.c), so under -c=native the
// captured `#include <limits.h>` replays verbatim and resolves to the
// *host*'s own header at native-compile time -- macOS's MB_LEN_MAX is 6,
// glibc's is 16. Asserting CCCC's own chosen value (16, sized to cover
// every supported host -- see include/limits.h's own comment) would pass on
// the VM and fail natively on macOS. Only what C itself guarantees
// (MB_LEN_MAX >= 1) is checked here.
//
// CHAR_MIN/CHAR_MAX are asserted equal to SCHAR_MIN/SCHAR_MAX: CCCC's
// ty_char (src/type.c) is signed on every supported platform, and
// -c=native unconditionally forwards -fsigned-char to the host cc
// (run_native_backend(), src/main.c, #1064), so this holds on both paths.
// The runtime (char)-1 < 0 check ties the macros to actual char codegen,
// not just the header's own macro text, so a future signedness/-fsigned-char
// desync would be caught here too.

#include <limits.h>

#ifndef CHAR_MIN
#error "CHAR_MIN missing from limits.h"
#endif
#ifndef CHAR_MAX
#error "CHAR_MAX missing from limits.h"
#endif
#ifndef MB_LEN_MAX
#error "MB_LEN_MAX missing from limits.h"
#endif

_Static_assert(CHAR_MIN == SCHAR_MIN, "plain char must be signed on every CCCC platform");
_Static_assert(CHAR_MAX == SCHAR_MAX, "plain char must be signed on every CCCC platform");
_Static_assert(MB_LEN_MAX >= 1, "C17 5.2.4.2.1p1 requires MB_LEN_MAX >= 1");

int main(void) {
    if (!((char)-1 < 0))
        return 1;

    char lo = CHAR_MIN;
    char hi = CHAR_MAX;
    if (lo != CHAR_MIN || hi != CHAR_MAX)
        return 2;
    if ((int)lo >= 0 || (int)hi <= 0)
        return 3;

    return 42;
}
