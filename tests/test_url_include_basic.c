// Test basic URL include functionality
// This test is only exercised when CCCC is built with libcurl
// (__CCCC_HAS_CURL__ is predefined for guest code in that case); otherwise it
// falls through to the same trivially-passing fallback as
// test_url_embed_basic.c.
//
// The URL uses the quoted form: clang-format rewrites "//" inside an
// angle-bracket path into a line comment ("https://" becomes "https: //"),
// which would break the URL. Quoted paths are single string literals and
// survive formatting untouched.

#ifdef __CCCC_HAS_CURL__

// For testing purposes, we'll use a simple header from a known stable URL.
// In real usage, this could be any valid C header file. Only macros are
// probed -- the header's functions have no implementation behind them in
// the VM (stb_sprintf's body lives in stb_sprintf.c).

#include "https://raw.githubusercontent.com/nothings/stb/master/stb_sprintf.h"

int main() {
    // If the include worked, the header's own macros are visible
#if !defined(STB_SPRINTF_H_INCLUDE) || STB_SPRINTF_MIN != 512
#error "URL include did not supply stb_sprintf.h"
#endif
    return 42;
}

#else

// Fallback test when curl is not available
int main() {
    return 42;
}

#endif
