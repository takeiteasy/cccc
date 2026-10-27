// CCCC_FLAGS: --checked-pointers
// Hoisted sibling arguments of every by-value class reach the callee intact.

typedef struct {
    int n;
} Small;
typedef struct {
    double a, b;
} Hfa;
typedef struct {
    long n;
    long pad[5];
} Big;
typedef union {
    int  n;
    long wide;
} Un;

static int calls;
static Small mk_small(void) {
    calls++;
    return (Small){2};
}
static Hfa mk_hfa(void) {
    calls++;
    return (Hfa){1.0, 2.0};
}
static Big mk_big(void) {
    calls++;
    return (Big){2, {0}};
}
static Un mk_un(void) {
    calls++;
    Un u;
    u.wide = 2;
    return u;
}
static long double mk_ld(void) {
    calls++;
    return 2.0L;
}

static int use_small(int *[[cccc::array, cccc::count(s.n)]] p, Small s) {
    return p[0] + s.n;
}
static int use_hfa(int *[[cccc::array, cccc::count((int)(h.a + h.b) - 1)]] p,
                   Hfa h) {
    return p[0] + p[1] + (int)h.b;
}
static int use_big(int *[[cccc::array, cccc::count(b.n)]] p, Big b) {
    return p[1] + (int)b.n;
}
static int use_un(int *[[cccc::array, cccc::count(u.n)]] p, Un u) {
    return p[0] + u.n;
}
static int use_ld(int * [[ cccc::array, cccc::count((int)d) ]] p,
                  long double d) {
    return p[1] + (int)d;
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){20, 10};
    int sum = use_small(small, mk_small()) + use_hfa(small, mk_hfa()) +
              use_big(small, mk_big()) + use_un(small, mk_un()) +
              use_ld(small, mk_ld());
    // 22 + 32 + 12 + 22 + 12 == 100
    return sum - 100 + 42 + (calls == 5 ? 0 : 100);
}
