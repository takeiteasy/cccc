// #1324: an ordinary project header (not a command-line input, not one of
// CCCC's own bundled/cccc-only headers) that itself carries a URL #include.
// See tests/test_url_include_nested_1324.c for what this exercises.
//
// The URL uses the quoted form: clang-format rewrites "//" inside an
// angle-bracket path into a line comment ("https://" becomes "https: //"),
// which would break the URL. Quoted paths are single string literals and
// survive formatting untouched.

#ifdef __CCCC_HAS_CURL__
#include "https://raw.githubusercontent.com/nothings/stb/master/stb_sprintf.h"
#endif
