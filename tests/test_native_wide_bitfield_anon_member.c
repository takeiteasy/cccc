// Anonymous struct/union members of a struct/union with a _BitInt(N>128)
// bit-field are reached at their own offsets, whether the anonymous member
// sits in the opaque aggregate or contains it.
struct WithAnon {
    int z;
    _BitInt(256) f : 100;
    struct {
        int a;
        int b;
    };
    union {
        char c;
        long l;
    };
};

struct Outer {
    int x;
    struct {
        _BitInt(256) g : 100;
        int h;
    };
    int y;
};

struct Nested {
    char c;
    union {
        _BitInt(256) f : 100;
        long l;
    };
    struct {
        int a;
        struct {
            int b;
            _BitInt(256) h : 33;
        };
    };
};

int main(void) {
    struct WithAnon s = {0};
    s.z               = 1;
    s.a               = 3;
    s.b               = 4;
    s.l               = 77;
    s.f               = -5;
    if (s.z != 1 || s.a != 3 || s.b != 4 || s.l != 77 || s.f != -5)
        return 1;

    struct Outer o = {0};
    o.x            = 1;
    o.g            = -9;
    o.h            = 8;
    o.y            = 2;
    if (o.x != 1 || o.g != -9 || o.h != 8 || o.y != 2)
        return 2;

    struct Nested n = {0};
    n.c             = 9;
    n.f             = 11;
    n.a             = 2;
    n.b             = 3;
    n.h             = -1;
    if (n.c != 9 || n.f != 11 || n.a != 2 || n.b != 3 || n.h != -1)
        return 3;
    return 42;
}
