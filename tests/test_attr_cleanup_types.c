// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: (?=(?:[\s\S]*?does not apply to types){5})(?=(?:[\s\S]*?attribute ignored){4})
//
// cleanup applies only to the automatic variable that names it. In a
// type-name (cast, compound literal, sizeof, typeof) it warns and does
// nothing; on a prototype parameter it is ignored; after a declarator's `*`
// it belongs to the variable.

static int n;
static void rel(int *p) {
    n += *p;
}
static void relp(int **p) {
    (void)p;
    n += 100;
}

void proto_named(int p __attribute__((cleanup(rel))));
void proto_unnamed(int __attribute__((cleanup(rel))));
void proto_leading(__attribute__((cleanup(rel))) int p);
void proto_c23(int p [[gnu::cleanup(rel)]]);

int main(void) {
    int  x = 1;
    int *a = &x;
    {
        int *lit = &(int __attribute__((cleanup(rel)))){1};
        __typeof__((int *__attribute__((cleanup(rel))))a) b = a;
        int s = sizeof(int __attribute__((cleanup(rel))));
        int c = (int [[gnu::cleanup(rel)]])1;
        int d = (int __attribute__((cleanup(rel))))1;
        (void)lit, (void)b, (void)s, (void)c, (void)d;
    }
    if (n)
        return 1;
    {
        int *__attribute__((cleanup(relp))) p = a;
        (void)p;
    }
    return n == 100 ? 42 : 2;
}
