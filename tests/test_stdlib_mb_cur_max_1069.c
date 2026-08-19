// Ticket #1069: include/stdlib.h had no MB_CUR_MAX at all (C17 7.22.1
// requires it), leaving the standard `char buf[MB_LEN_MAX]` +
// `mbtowc()`/`wctomb()` idiom unusable, even after #1067 added MB_LEN_MAX
// to limits.h. Unlike MB_LEN_MAX (a compile-time upper bound), MB_CUR_MAX
// is genuinely locale-dependent at runtime -- glibc implements it as a
// function call (__ctype_get_mb_cur_max()), macOS as a plain global
// (__mb_cur_max).
//
// #ifndef/#error below so this fails to *compile* against the pre-fix
// header, not just fails at runtime -- confirmed against the pre-fix
// binary before landing (this batch's own recurring lesson: don't add a
// test that would pass vacuously green).
//
// This file deliberately does NOT assert a specific MB_CUR_MAX value.
// stdlib.h is on neither is_compiler_owned_header nor
// is_cccc_supplied_only_header (src/preprocess.c), so under -c=native the
// captured `#include <stdlib.h>` replays verbatim and resolves to the
// *host*'s own header at native-compile time -- only what C itself
// guarantees (1 <= MB_CUR_MAX <= MB_LEN_MAX, and MB_CUR_MAX == 1 in the
// "C" locale) is checked. Buffers use MB_LEN_MAX (a compile-time constant),
// not MB_CUR_MAX (a runtime value -- sizing an array with it would be a
// VLA, and VLA-length expressions are a separate, known-shaky area under
// -c=native, #1042(d); not what this test is checking).

#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <wchar.h>

int main(void) {
    // C99/C17 7.11.1.1: a program starts in the "C" locale, where
    // MB_CUR_MAX must be exactly 1. setlocale(LC_ALL, "C") pins this
    // explicitly rather than relying on the ambient locale (which could
    // be UTF-8 in some environments), so this holds everywhere the suite
    // runs, including a minimal container.
    setlocale(LC_ALL, "C");
    if (MB_CUR_MAX != 1)
        return 1;
    if (MB_CUR_MAX < 1 || MB_CUR_MAX > MB_LEN_MAX)
        return 2;

    // A single-byte char round-trips through mbtowc/wctomb under the "C"
    // locale, ties MB_CUR_MAX to the actual mb* passthroughs rather than
    // just the macro's own text.
    char buf[MB_LEN_MAX];
    wchar_t wc = 0;
    int n = mbtowc(&wc, "A", MB_CUR_MAX);
    if (n != 1 || wc != L'A')
        return 3;
    n = wctomb(buf, wc);
    if (n != 1 || buf[0] != 'A')
        return 4;

    // Try a UTF-8 locale too, but only if the host actually has one
    // installed -- a minimal container may not, and this test isn't
    // checking locale availability, just that MB_CUR_MAX moves in step
    // with whichever locale setlocale() actually switched to.
    if (setlocale(LC_ALL, "en_US.UTF-8") != NULL ||
        setlocale(LC_ALL, "C.UTF-8") != NULL) {
        if (MB_CUR_MAX < 1 || MB_CUR_MAX > MB_LEN_MAX)
            return 5;
        setlocale(LC_ALL, "C");
    }

    return 42;
}
