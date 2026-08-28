// -c=native regression (#1207): a global union initializer where no member
// spans the union's full (alignment-padded) size used to hit "cannot
// serialize initializer ... union has no member spanning the full N-byte
// object". Every union member sits at offset 0, so the untouched tail past
// the largest member is always zero, relocation-free alignment padding
// (gvar_initializer() allocates init_data at exactly the union's size and
// memsets it to 0 before write_gvar_data() runs) -- a host compiler
// zero-fills that tail for static storage the same way it already does for
// ordinary struct padding, so no member needs to span the whole object for
// a byte-exact reconstruction. Covers: the shape from the ticket, a union
// nested inside a struct (exercises a non-zero `offset` into init_data),
// and a union whose largest member is itself an anonymous struct (which
// used to segfault outright, unrelated to #1207 itself -- serialize_decl.c
// dereferenced largest->name unguarded; fixed alongside #1207 by
// flattening anonymous members into transparent designators, C11
// 6.7.2.1p13).
#include <stdio.h>

union U1 {
    char  c[3]; // 3 bytes, largest by size
    short s;    // 2 bytes, but forces 2-byte alignment -> sizeof(U1) == 4
};
union U1 g1 = {.c = {1, 2, 3}};

union U2 {
    char c[5]; // 5 bytes, largest by size
    int  x;    // 4 bytes, but forces 4-byte alignment -> sizeof(U2) == 8
};
union U2 g2 = {.c = {1, 2, 3, 4, 5}};

struct Wrapper {
    int      tag;
    union U1 u; // nested union at a non-zero struct offset
};
struct Wrapper g3 = {.tag = 7, .u = {.c = {9, 8, 7}}};

union U4 {
    struct {
        int a;
        int b;
    }; // anonymous struct member, largest by size (8 bytes)
    char c; // 1 byte
};
union U4 g4 = {.a = 10, .b = 20};

// An anonymous, zero-size union member -- flattening it (serialize_
// agg_member_list's TY_STRUCT arm) recurses into the TY_UNION arm for a
// 0-byte union, which must return with nothing to designate rather than
// hard-error on "no members" (the ordinary `!largest` check is for a
// genuine internal inconsistency, not this case).
struct Wrapper2 {
    union {};
    int x;
};
struct Wrapper2 g5 = {.x = 99};

int main(void) {
    if (sizeof(g1) != 4 || g1.c[0] != 1 || g1.c[1] != 2 || g1.c[2] != 3)
        return 1;
    if (sizeof(g2) != 8 || g2.c[0] != 1 || g2.c[4] != 5)
        return 2;
    if (g3.tag != 7 || g3.u.c[0] != 9 || g3.u.c[2] != 7)
        return 3;
    if (g4.a != 10 || g4.b != 20)
        return 4;
    if (g5.x != 99)
        return 5;
    printf("padded union initializers ok\n");
    return 42;
}
