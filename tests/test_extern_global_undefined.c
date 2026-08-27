// EXPECT_COMPILE_ERROR
// In -c mode, this is still a hard error rather than deferred to link
// time -- there is no name-based data relocation mechanism for globals
// (unlike the one functions get), so it's suppressed, not deferred.
// CCCC_EXPECT_STDERR: undefined global: g
//
// #957: a referenced extern global that is never defined anywhere must be
// a hard compile error, mirroring "undefined function: %s" for the
// function case (tests/test_extern_undefined.c).
extern int g;

int main(void) {
    return g;
}
