// CCCC_FLAGS: --std=c17
//
// GNU __auto_type: type inference from the initializer in every -std.

static __auto_type g                          = 40;
__auto_type        ga __attribute__((unused)) = 1;

static int         hits;
static void bump(int *p) {
    hits += *p;
}

int main(void) {
    int         a = 1;

    __auto_type t = a;
    if (sizeof(t) != sizeof(int) || t != 1)
        return 1;

    const int   c = 2;
    __auto_type d = c;
    d             = 3;
    if (d != 3)
        return 2;

    int         arr[2] = {5, 6};
    __auto_type p      = arr;
    if (sizeof(p) != sizeof(int *) || p[1] != 6)
        return 3;

    __auto_type dbl = 1.5;
    if (sizeof(dbl) != sizeof(double))
        return 4;

    static __auto_type s = 7;
    if (s != 7)
        return 5;

    const __auto_type cq = 8;
    if (cq != 8)
        return 6;

    __auto_type u __attribute__((unused)) = a;

    {
        __auto_type cl __attribute__((cleanup(bump))) = 10;
        (void)cl;
    }
    if (hits != 10)
        return 7;

    if (g != 40 || ga != 1)
        return 8;

    return 42;
}
