// Fixture for tests/test_serialize_vendored_multi_tu_include_1304.c (#1304).
// A vendored single-header library in the stb_sprintf.h/#1301 mold,
// re-included from TWO TUs -- this test's own point, unlike #1301's fixture
// (fixtures/vendored_1301_lib.h), whose second TU never re-includes the
// header at all. #include "fixtures/vendored_1304_lib.h" is identical text
// in both TUs, so it is the push-time route into the hazard
// (push_emit_directive's own #1304 fix, src/preprocess.c) rather than the
// #1292 two-different-spellings replay-time route.
#ifndef VENDORED_1304_LIB_H
#define VENDORED_1304_LIB_H

#define V1304_DECORATE(name) v1304_##name

int V1304_DECORATE(add)(int x);

#ifdef VENDORED_1304_IMPLEMENTATION

int V1304_DECORATE(add)(int x) {
    return x + 1;
}

#endif // VENDORED_1304_IMPLEMENTATION

#endif // VENDORED_1304_LIB_H
