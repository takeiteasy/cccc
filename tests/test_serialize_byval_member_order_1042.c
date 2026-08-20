// Ticket #1042(a): a struct/union first reached only through a POINTER
// reference (e.g. a function-pointer typedef parameter) gets its (then-
// incomplete) Type pushed into -c=native's ctx->defs at that early
// position. A BY-VALUE member of some other struct, collected in between,
// then needs the full body before it's actually available -- the minilua
// repro's own shape ('struct lua_State l;' inside 'struct LX', reached
// after 'lua_CFunction' == 'int (*)(struct lua_State *)' already pushed an
// incomplete stub): "field has incomplete type" from the host compiler.
//
// #1010's own repromotion swap in collect_type() (src/serialize.c) fires
// once the real definition is reached, but re-pushes at the TAIL of
// ctx->defs -- after every entry collected since, including a later
// by-value user of the same type. Fixed with a stable topological reorder
// pass over ctx->defs, run once after collection: an edge for every
// by-value (non-pointer) member forces its type's own definition ahead of
// its user, without disturbing any pair whose order was already legal.
//
// Real member values (not just "it compiles") are asserted below, so a
// wrong layout -- not just a host compile failure -- would fail this test
// too.

#include <stdio.h>

// Pointer reference FIRST (mirrors lua_CFunction): pushes an incomplete
// stub for `struct Early` into collect_type()'s walk before any by-value
// use is ever reached.
typedef int (*EarlyFn)(struct Early *);

struct Early {
    int  tag;
    long value;
};

// A by-value member of Early, collected AFTER the pointer reference above
// -- the exact ordering hazard #1042(a) fixes.
struct Holder {
    char         prefix[8];
    struct Early e;
};

static int use_early(struct Early *e) {
    return e->tag;
}

static struct Early make_early(int tag, long value) {
    struct Early e;
    e.tag   = tag;
    e.value = value;
    return e;
}

// Control: a struct whose by-value member's type is ALREADY fully defined
// ahead of its use -- the ordinary, already-legal case. The reorder pass
// must leave this alone (a Kahn's-algorithm "ready node" never moves).
struct Base {
    int x;
};

struct Wrapper {
    struct Base b;
    int         y;
};

int main(void) {
    EarlyFn fn = use_early; // keep the pointer reference genuinely live
    (void)fn;

    struct Holder h;
    h.prefix[0] = 'H';
    h.e         = make_early(7, 123456789L);
    if (h.e.tag != 7)
        return 1;
    if (h.e.value != 123456789L)
        return 2;
    if (sizeof(h.e) != sizeof(struct Early))
        return 3;

    struct Wrapper w;
    w.b.x = 5;
    w.y   = 6;
    if (w.b.x != 5 || w.y != 6)
        return 4;

    return 42;
}
