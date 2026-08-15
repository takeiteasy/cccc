// Shared header for tests/test_serialize_opaque_handle_1010.c and
// tests/test_serialize_opaque_handle_1010_rev.c (#1010).
//
// The opaque-handle idiom: this header only forward-declares the tag via a
// typedef -- exactly one translation unit ever supplies `struct DyAtoms1010
// { ... };` (which .c that is differs between the two tests, see their own
// comments), every other TU only ever sees the incomplete type below.
#pragma once
typedef struct DyAtoms1010 DyAtoms1010;
DyAtoms1010 *make_atoms_1010(void);
int get_x_1010(DyAtoms1010 *t);
