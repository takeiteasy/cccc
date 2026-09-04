// Shared header for tests/test_serialize_dup_typedef_1290.c (#1290).
//
// A tagless typedef struct -- unlike #1014's opaque-handle idiom (a shared
// header forward-declaring a *tagged* struct, each TU completing its own
// body), this is src/json.c's own real shape: the header's copy is the
// ONLY definition meant to reach the host compiler, and some other TU
// having independently declared the identical name/shape must not print a
// second one.
#pragma once
typedef struct {
    int a;
    int b;
} DupTypedef1290;

int dup_typedef_1290_sum(DupTypedef1290 v);
