// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __cccc_c8state
//
// #1141: <uchar.h>'s C11/C23 conversions (mbrtoc16/c16rtomb/mbrtoc32/
// c32rtomb/mbrtoc8/c8rtomb) are VM cfuncs (src/stdlib/wide.c) with no
// definition reaching -c=native's output at all -- uchar.h is on
// is_cccc_supplied_only_header() (preprocess.c), so its declarations are
// re-derived but nothing ever defined the functions themselves. glibc has
// shipped the c16/c32 pair since 2.16 and the c8 pair since 2.36, so a
// host new enough links against the real symbol with no help needed; but
// Darwin has never shipped any of the six, and calling one there failed
// at the host linker ("Undefined symbols ... _c16rtomb"). Fixed by
// serialize_uchar_shims() (src/serialize.c), a self-contained fallback
// definition emitted for exactly the functions used, guarded by the same
// __GLIBC_PREREQ feature test src/stdlib/wide.c itself already uses to
// choose between the real symbol and its own VM-side fallback -- so a
// host with the real symbols never sees a second, competing definition.
// The behavioural half is exercised by test_suite_strings.c's own
// test_wchar_uchar_headers, which this un-skips from the native corpus.
#include <uchar.h>
#include <wchar.h>

int main(void) {
    mbstate_t st;
    __builtin_memset(&st, 0, sizeof(st));

    char   buf[8] = {0};
    size_t rc     = c16rtomb(buf, u'A', &st);
    (void)rc;

    __builtin_memset(&st, 0, sizeof(st));
    rc = c32rtomb(buf, U'B', &st);
    (void)rc;

    __builtin_memset(&st, 0, sizeof(st));
    rc = c8rtomb(buf, (char8_t)'C', &st);
    (void)rc;

    return 42;
}
