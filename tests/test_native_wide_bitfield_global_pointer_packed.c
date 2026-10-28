// A pointer at a packed (misaligned) offset, or in a union, is stored by a
// startup constructor, including in a const global.
static int fn(void) {
    return 7;
}
int x = 5;
int arr[8];

struct __attribute__((packed)) P {
    char c;
    int *p;
    _BitInt(256) f : 100;
};

struct __attribute__((packed)) Q {
    char c;
    int (*fp)(void);
    const char *s;
    int        *e;
    _BitInt(256) f : 100;
};

union U {
    int *p;
    long l;
    _BitInt(256) f : 100;
};

struct Outer {
    int     tag;
    union U u;
};

struct P       gp     = {1, &x};
struct Q       gq     = {2, fn, "hi", &arr[3]};
struct Outer   go     = {9, {.p = &x}};
const struct P gcp    = {3, &x};
union U        gu[2]  = {{.p = &x}, {.p = &arr[2]}};
const union U  gcu[2] = {{.p = &x}, {.p = &arr[1]}};

static int     early;
__attribute__((constructor)) static void snoop(void) {
    early = gp.p == &x && go.u.p == &x;
}

int main(void) {
    if (!early)
        return 1;
    if (gp.c != 1 || gp.p != &x || *gp.p != 5)
        return 2;
    if (gq.c != 2 || gq.fp() != 7 || gq.s[1] != 'i' || gq.e != &arr[3])
        return 3;
    if (go.tag != 9 || go.u.p != &x)
        return 4;
    if (gcp.c != 3 || gcp.p != &x)
        return 5;
    if (gu[0].p != &x || gu[1].p != &arr[2])
        return 6;
    if (gcu[0].p != &x || gcu[1].p != &arr[1])
        return 7;
    return 42;
}
