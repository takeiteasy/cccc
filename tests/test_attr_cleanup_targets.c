// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: 'cleanup' attribute ignored
//
// cleanup applies only to automatic variables, matching gcc: a leading
// attribute covers every declarator, a declarator's own attribute covers just
// that variable, a declaration typed from a cleanup variable does not inherit
// it, and a typedef, member, global, static local or parameter ignores it
// with a warning.

static int n;
static void rel(int *p) {
    n += *p;
}
static void relp(int **p) {
    n += **p;
}

typedef int T __attribute__((cleanup(rel)));
struct S {
    int x __attribute__((cleanup(rel)));
};
static int g __attribute__((cleanup(rel)));

static void param(int p __attribute__((cleanup(rel)))) {
    (void)p;
}

int main(void) {
    {
        int a __attribute__((cleanup(rel))) = 10, unused = 100;
        __attribute__((cleanup(rel))) int b = 5, c = 5;
        int                               x = 10, y = 10;
        int __attribute__((cleanup(relp))) *p = &x;
        int z = 0, __attribute__((cleanup(relp))) *q = &y;
        {
            __typeof__(a)  d = 100;
            typeof(a + 0)  e = 100;
            __typeof__(*p) f = 100;
            __typeof__(*q) h = 100;
            (void)d, (void)e, (void)f, (void)h;
        }
        if (n)
            return 1;
        T               t                                = 100;
        struct S        s                                = {100};
        __typeof__(s.x) m                                = 100;
        __typeof__(g)   k                                = 100;
        static int      st __attribute__((cleanup(rel))) = 100;
        param(100);
        (void)unused, (void)b, (void)c, (void)z;
        (void)t, (void)m, (void)k, (void)st;
    }
    return n + 2;
}
