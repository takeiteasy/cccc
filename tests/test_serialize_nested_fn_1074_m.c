// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __static_link
// CCCC_EXPECT_STDOUT: __cccc_nenv
// -m output threads the static link through nested-function call sites.
static int outer_no_upvar(int base) {
    int inner_no_upvar(int x) {
        return x * 2;
    }
    return inner_no_upvar(base);
}
static int outer_rw_local(void) {
    double v = 3.14;
    void set_v(double val) {
        v = val;
    }
    set_v(99.9);
    return (v == 99.9) ? 1 : 0;
}
static int outer_multilevel(void) {
    int g = 5;
    int mid(int m) {
        int inner_multilevel(int n) {
            return n + g;
        }
        return inner_multilevel(m) * 2;
    }
    return mid(10);
}
static int outer_recursive(void) {
    int fact(int n) {
        if (n <= 1)
            return 1;
        return n * fact(n - 1);
    }
    return fact(5);
}
static int outer_siblings(void) {
    int a = 1, b = 2;
    int get_a(void) {
        return a;
    }
    int get_b(void) {
        return b;
    }
    int sum_siblings(void) {
        return get_a() + get_b();
    }
    return sum_siblings();
}
int main(void) {
    if (outer_no_upvar(21) != 42)
        return 1;
    if (outer_rw_local() != 1)
        return 2;
    if (outer_multilevel() != 30)
        return 3;
    if (outer_recursive() != 120)
        return 4;
    if (outer_siblings() != 3)
        return 5;
    return 42;
}
