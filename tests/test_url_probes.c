// Test URL feature-detection probes: __has_include(<https://...>) and
// __has_embed(<https://...>) resolve through the same shared cache a real
// #include/#embed uses, so both agree with each other and answer what an
// actual fetch would do. Only exercised in curl-enabled builds
// (__CCCC_HAS_CURL__ is predefined for guest code in that case); otherwise
// it falls through to the same trivially-passing fallback as
// test_url_include_basic.c.
//
// URLs use the quoted form: clang-format rewrites "//" inside an
// angle-bracket path into a line comment ("https://" becomes "https: //"),
// which would break the URL. Quoted paths are single string literals and
// survive formatting untouched.

#ifdef __CCCC_HAS_CURL__

// clang-format off: keep each probe directive on one line (a raw newline
// inside #if ends the directive; clang-format would wrap these).
#if !__has_include("https://raw.githubusercontent.com/nothings/stb/master/stb_sprintf.h")
#error "__has_include should report 1 for a fetchable URL"
#endif

// stb_sprintf.h is non-empty, so the embed probe reports 1 (found,
// non-empty) -- and agrees with __has_include on the same URL.
#if __has_embed("https://raw.githubusercontent.com/nothings/stb/master/stb_sprintf.h") != 1
#error "__has_embed should report 1 for a fetchable non-empty URL"
#endif

// A well-formed but absent URL reports 0 from both probes. This is the one
// case that costs a network round-trip per probe (a 404), so keep it to a
// single pair.
#if __has_include("https://raw.githubusercontent.com/nothings/stb/master/no_such_header_cccc_probe.h") != 0
#error "__has_include should report 0 for an unfetchable URL"
#endif

#if __has_embed("https://raw.githubusercontent.com/nothings/stb/master/no_such_header_cccc_probe.h") != 0
#error "__has_embed should report 0 for an unfetchable URL"
#endif
// clang-format on

int main() {
    return 42;
}

#else

// Fallback when curl is not available
int main() {
    return 42;
}

#endif
