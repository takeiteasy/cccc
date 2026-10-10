// A struct/union with a bit-field whose declared type is a _BitInt wider than
// 128 bits round-trips through -c=native: members of such an aggregate are
// read and written at CCCC's own offsets.
#include <stddef.h>

struct A {
    _BitInt(256) f : 193;
};
struct B {
    _BitInt(256) a : 20;
    _BitInt(256) b : 193;
};
struct In {
    int  v;
    char c;
};
struct Mixed {
    char c;
    unsigned _BitInt(256) w : 130;
    int      x;
    unsigned n     : 5;
    _BitInt(192) m : 150;
    struct In in;
    int       arr[3];
};
struct Packed {
    char c;
    _BitInt(192) f : 150;
    int tail;
} __attribute__((packed));
union U {
    _BitInt(256) f : 200;
    long l;
};
struct Outer {
    int      tag;
    struct B inner;
};

static struct B gb      = {-5, ((_BitInt(256))1 << 150) + 11};
struct Outer    go      = {7, {3, -((_BitInt(256))1 << 100)}};
struct B        garr[2] = {{1, 2}, {-1, -2}};

static struct B make_b(_BitInt(256) a, _BitInt(256) b) {
    struct B r;
    r.a = a;
    r.b = b;
    return r;
}

static long sum_b(struct B v) {
    return (long)v.a + (long)v.b;
}

int main(void) {
    struct A a;
    a.f = 3;
    if (a.f != 3)
        return 1;
    a.f = -3;
    if (a.f != -3)
        return 2;
    if ((a.f = 9) != 9)
        return 3;

    struct B b;
    b.a = -5;
    b.b = ((_BitInt(256))1 << 150) + 11;
    if (b.a != gb.a || b.b != gb.b)
        return 4;
    b.b = 0;
    if (b.a != -5)
        return 5; // writing b.b leaves the neighbour alone
    b.a = 0x7ffff;
    if (b.a != 0x7ffff || b.b != 0)
        return 6;

    struct Mixed m = {0};
    m.c            = 'x';
    m.w            = ~(unsigned _BitInt(256))0; // truncated to 130 bits
    m.x            = 77;
    m.n            = 21;
    m.m            = -((_BitInt(192))1 << 140);
    m.in.v         = 5;
    m.in.c         = 'q';
    m.arr[2]       = 9;
    if (m.w != (((unsigned _BitInt(256))1 << 130) - 1))
        return 7;
    if (m.c != 'x' || m.x != 77 || m.n != 21)
        return 8;
    if (m.m != -((_BitInt(192))1 << 140))
        return 9;
    if (m.in.v != 5 || m.in.c != 'q' || m.arr[2] != 9 || m.arr[0] != 0)
        return 10;
    int *px  = &m.x;
    *px     += 1;
    if (m.x != 78)
        return 11;
    m.n += 3;
    m.n++;
    if (m.n != 25)
        return 12;
    struct In *pin = &m.in;
    pin->v         = 6;
    if (m.in.v != 6)
        return 13;

    a.f += 5;
    a.f++;
    if (a.f != 15)
        return 14;
    struct A *pa = &a;
    pa->f        = 100;
    if (pa->f != 100 || a.f != 100)
        return 15;

    struct Packed pk;
    pk.c    = 'p';
    pk.f    = -((_BitInt(192))1 << 120);
    pk.tail = 0x1234;
    if (sizeof(pk) != 24 || offsetof(struct Packed, tail) != 20 ||
        _Alignof(struct Packed) != 1)
        return 16;
    if (pk.c != 'p' || pk.f != -((_BitInt(192))1 << 120) || pk.tail != 0x1234)
        return 17;

    union U u;
    u.l = 0;
    u.f = 12345;
    if (u.f != 12345)
        return 18;
    u.l = -1; // the low 64 bits of f; the rest is untouched
    if (u.f != (((_BitInt(256))1 << 64) - 1))
        return 19;

    struct Outer o = {0};
    o.tag          = 4;
    o.inner.a      = 8;
    o.inner.b      = -77;
    if (o.tag != 4 || o.inner.a != 8 || o.inner.b != -77)
        return 20;
    if (go.tag != 7 || go.inner.a != 3 ||
        go.inner.b != -((_BitInt(256))1 << 100))
        return 21;
    if (garr[0].a != 1 || garr[0].b != 2 || garr[1].a != -1 || garr[1].b != -2)
        return 22;

    struct B copy = b;
    if (copy.a != b.a || copy.b != b.b)
        return 23;
    copy = make_b(6, -7);
    if (copy.a != 6 || copy.b != -7)
        return 24;
    if (make_b(30, 12).b != 12)
        return 25; // member of an rvalue
    if (sum_b(copy) != -1)
        return 26;

    if (sizeof(struct A) != 32 || _Alignof(struct A) != 8)
        return 27;
    if (offsetof(struct Mixed, x) != 20)
        return 28;

    return 42;
}
