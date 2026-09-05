// Fixture for tests/test_serialize_vendored_macro_defined_outside_1308.c
// (#1308). A vendored-single-header-library-shaped body whose token-pasted
// declarator macro (DECORATE_1308) is invoked here but declared OUTSIDE
// this header entirely -- in the includer .c file -- unlike fixtures/
// vendored_1301_lib.h, whose own V1301_DECORATE is defined inside the
// header itself.
#ifndef VENDORED_1308_MACRO_OUTSIDE_LIB_H
#define VENDORED_1308_MACRO_OUTSIDE_LIB_H

int DECORATE_1308(add)(int x);

#ifdef VENDORED_1308_IMPLEMENTATION

int DECORATE_1308(add)(int x) {
    return x + 1;
}

#endif // VENDORED_1308_IMPLEMENTATION

#endif // VENDORED_1308_MACRO_OUTSIDE_LIB_H
