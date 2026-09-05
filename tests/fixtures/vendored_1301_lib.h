// Fixture for tests/test_serialize_vendored_single_header_1301.c (#1301).
// A vendored single-header library in the stb_sprintf.h/#1298-follow-up
// mold: include-guarded, always exposes bodiless prototypes, and under an
// IMPLEMENTATION-style macro (defined in exactly one TU before this header
// is included) also provides the definitions. Its declarator names are
// token-pasted -- V1301_DECORATE(name) mirrors stb_sprintf.h's own
// STBSP__PUBLICDEF-adjacent DECORATE idiom -- so a bare file-identity check
// on the declarator's own spelling location (the macro's defining file,
// this header) can't be trusted; only the walk to its *expansion* site
// (still this header, since the macro is invoked right here) can.
#ifndef VENDORED_1301_LIB_H
#define VENDORED_1301_LIB_H

#define V1301_DECORATE(name) v1301_##name

int V1301_DECORATE(add)(int x);

#ifdef VENDORED_1301_IMPLEMENTATION

int V1301_DECORATE(add)(int x) {
    return x + 1;
}

#endif // VENDORED_1301_IMPLEMENTATION

#endif // VENDORED_1301_LIB_H
