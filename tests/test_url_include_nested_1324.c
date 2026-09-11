// #1324: a URL #include reached only through an ordinary project header
// (test_url_include_nested_1324.h here, not this command-line input file
// itself) used to reach a real host compiler unrewritten under -c=native/-m/
// -c=generated -- the auto-capture gate (src/preprocess.c) only captures a
// directive whose includer is a command-line input or one of CCCC's own
// bundled/cccc-only headers, so nothing ever registered this directive for
// #1313's rewrite. Fixed by mirroring every fetched URL on disk under a
// URL-shaped path (fetch_url_to_cache(), src/url_fetch.c) and forwarding
// that directory to the host cc (-idirafter, src/main.c), so the raw
// directive resolves as written regardless of which file it's in.
//
// This test is only exercised when CCCC is built with libcurl
// (__CCCC_HAS_CURL__ is predefined for guest code in that case); otherwise it
// falls through to a trivially-passing fallback, same as
// test_url_include_basic.c.

#include "test_url_include_nested_1324.h"

#ifdef __CCCC_HAS_CURL__
int main() {
#if !defined(STB_SPRINTF_H_INCLUDE) || STB_SPRINTF_MIN != 512
#error "nested URL include did not supply stb_sprintf.h"
#endif
    return 42;
}
#else
int main() {
    return 42;
}
#endif
