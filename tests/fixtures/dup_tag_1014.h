// Shared header for tests/test_serialize_dup_tag_1014*.c (#1014).
//
// The opaque-handle idiom used per-backend: this header only forward-
// declares the tag via a typedef -- more than one translation unit
// independently completes `struct DyGC1014 { ... };` with a different
// shape, exactly the shape ~takeiteasy/dandy's gc_none.c/gc_tracing.c use
// (each privately defining their own struct DyGC behind one shared opaque
// handle).
#pragma once
typedef struct DyGC1014 DyGC1014;
DyGC1014 *gc_open_1014(void);
int gc_val_1014(DyGC1014 *g);
