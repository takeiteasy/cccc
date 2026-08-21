// Test basic URL #embed functionality
// This test is only exercised when CCCC is built with libcurl
// (__CCCC_HAS_CURL__ is predefined for guest code in that case); otherwise it
// falls through to the same trivially-passing fallback as
// test_url_include_basic.c.
//
// The URL uses the quoted form: clang-format rewrites "//" inside an
// angle-bracket path into a line comment ("https://" becomes "https: //"),
// which would break the URL. Quoted paths are single string literals and
// survive formatting untouched.

#ifdef __CCCC_HAS_CURL__

// Embed the first bytes of a file fetched from a URL. stb_sprintf.h begins
// with a "// " comment, so limit(2) yields exactly '/', '/'.
// clang-format off: keep the directive on one line (clang-format would
// wrap it with backslash continuations).
static const unsigned char url_bytes[] = {
#embed "https://raw.githubusercontent.com/nothings/stb/master/stb_sprintf.h" limit(2)
};

int main() {
    if (sizeof(url_bytes) != 2)
        return 1;
    if (url_bytes[0] != '/' || url_bytes[1] != '/')
        return 1;
    return 42;
}

#else

// Fallback when curl is not available
int main() {
    return 42;
}

#endif
