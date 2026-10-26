// CCCC_FLAGS: --checked-pointers
// Redeclarations that spell the same bounds agree: renamed sibling
// parameters, redundant parentheses and a bound naming a global.

int g = 2;

void a(int *[[cccc::array, cccc::count(n)]] p, int n);
void a(int *[[cccc::array, cccc::count(len)]] q, int len) {
    (void)q;
    (void)len;
}

void b(int *[[cccc::array, cccc::count(n)]] p, int n);
void b(int *[[cccc::array, cccc::count((n))]] p, int n) {
    (void)p;
    (void)n;
}

void c(int *[[cccc::array, cccc::count(g)]] p);
void c(int *[[cccc::array, cccc::count(g)]] p) {
    (void)p;
}

int *[[cccc::array, cccc::count(n)]] mk(int n);
int *[[cccc::array, cccc::count(n)]] mk(int n) {
    return 0;
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] s = (int[2]){1, 2};
    a(s, 2);
    b(s, 2);
    c(s);
    return 42;
}
