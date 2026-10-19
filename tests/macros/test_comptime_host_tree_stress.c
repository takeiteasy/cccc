// Every surviving tsearch node stays readable while tdelete removes keys in a
// scrambled order (nodes with two children included).
#include[[cccc::comptime]] < search.h>

[[cccc::comptime]]
static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

[[cccc::comptime]] static long g_sum;
[[cccc::comptime]] static int  g_count;

[[cccc::comptime]]
static void visit(const void *node, VISIT which, int depth) {
    (void)depth;
    if (which == postorder || which == leaf) {
        g_sum += **(int *const *)node;
        g_count++;
    }
}

[[cccc::comptime]]
int probe(void) {
    enum { N = 1000 };
    static int  keys[N];
    static char alive[N];
    void       *root = NULL;
    long        sum  = 0;
    int         bad  = 0;

    for (int i = 0; i < N; i++) {
        int k     = (i * 13) % N;
        keys[k]   = k + 1;
        alive[k]  = 1;
        sum      += k + 1;
        tsearch(&keys[k], &root, cmp_int);
    }

    for (int i = 0; i < N; i++) {
        int k = (i * 7 + 3) % N;
        tdelete(&keys[k], &root, cmp_int);
        alive[k]  = 0;
        sum      -= k + 1;
        if (i % 50 == 0 || i > N - 4) {
            g_sum   = 0;
            g_count = 0;
            twalk(root, visit);
            bad       += g_sum != sum;
            bad       += g_count != N - 1 - i;
            bad       += tfind(&keys[k], &root, cmp_int) != NULL;
            int other  = (k + 1) % N;
            if (alive[other])
                bad += tfind(&keys[other], &root, cmp_int) == NULL;
        }
    }
    return bad ? 1 : 42;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(probe())));
}

gen();

int main(void) {
    return result();
}
