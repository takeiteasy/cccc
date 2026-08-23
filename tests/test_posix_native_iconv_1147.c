// #1147: -c=native never linked -liconv, so a program calling
// iconv_open()/iconv()/iconv_close() failed at LINK time, not compile
// time, on macOS (glibc bundles iconv in libc itself, so this never
// affected Linux). Found while verifying #1140's own native-mode fix --
// once that ticket's nine undeclared-identifier diagnostics cleared, the
// native compile of tests/suites/test_suite_posix.c got further than ever
// before and hit this: previously invisible behind #1140's compile-time
// errors.
//
// Fixed by appending -liconv to the native cc invocation on Darwin only
// (src/main.c, alongside the existing unconditional -lm/-pthread), mirroring
// this project's own build (Makefile's Darwin-only LDFLAGS += -liconv).
//
// This is the same Latin-1-to-UTF-8 round trip as test_posix_iconv
// (tests/suites/test_suite_posix.c), given its own standalone file here
// because that suite stays in NATIVE_SKIP_TESTS (tools/testing/__init__.py)
// for the unrelated, still-open #1145 (14 subtests failing at native
// runtime on Linux/glibc), so it never actually exercises -c=native's own
// link step.
#include <iconv.h>

int main(void) {
    iconv_t cd = iconv_open("UTF-8", "ISO-8859-1");
    if (cd == (iconv_t)-1)
        return 1;

    char   in[1]   = {(char)0xE9}; // Latin-1 "e with acute"
    char   out[8]  = {0};
    char  *inp     = in;
    char  *outp    = out;
    size_t inleft  = 1;
    size_t outleft = sizeof(out);

    size_t rc      = iconv(cd, &inp, &inleft, &outp, &outleft);
    if (rc == (size_t)-1)
        return 2;
    if (inleft != 0)
        return 3;

    unsigned char b0 = (unsigned char)out[0];
    unsigned char b1 = (unsigned char)out[1];
    if (b0 != 0xC3 || b1 != 0xA9) // UTF-8 encoding of U+00E9
        return 4;

    if (iconv_close(cd) != 0)
        return 5;

    return 42;
}
