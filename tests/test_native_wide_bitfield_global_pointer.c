// A global initializer holding pointers inside a struct with a _BitInt(N>128)
// bit-field keeps the relocations.
int x;
int arr[8];
static int fn(void) {
    return 7;
}

struct S {
    int *p;
    _BitInt(256) f : 100;
};
struct S g = {&x, 1};

struct N {
    int tag;
    struct {
        const char *s;
        int (*cb)(void);
    } in;
    _BitInt(192) w : 150;
    int *tail;
};
struct N       gn      = {3, {"str", fn}, -4, &arr[3]};

const struct S gc      = {&arr[5], 2};
struct S       garr[2] = {{&x, 1}, {0, -2}};

struct P {
    int *ps[3];
    _BitInt(256) f : 100;
};
struct P gp = {{&arr[1], 0, &x}, 5};

struct O {
    char     c;
    struct S inner;
};
struct O go = {1, {&arr[2], 6}};

int main(void) {
    x      = 11;
    arr[1] = 22;
    arr[2] = 44;
    arr[3] = 33;
    arr[5] = 55;
    if (g.p != &x || *g.p != 11 || g.f != 1)
        return 1;
    if (gn.tag != 3 || gn.in.s[0] != 's' || gn.in.cb() != 7 || gn.w != -4 ||
        *gn.tail != 33)
        return 2;
    if (*gc.p != 55 || gc.f != 2)
        return 3;
    if (garr[0].p != &x || garr[1].p != 0 || garr[1].f != -2)
        return 4;
    if (*gp.ps[0] != 22 || gp.ps[1] || gp.ps[2] != &x || gp.f != 5)
        return 5;
    if (*go.inner.p != 44 || go.inner.f != 6 || go.c != 1)
        return 6;
    return 42;
}
