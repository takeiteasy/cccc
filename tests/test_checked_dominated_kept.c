// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_EXPECT_STDOUT: (CHKR .*){10}
// CCCC_REJECT_STDOUT: (CHKR .*){11}
//
// Each function keeps both checks (two apiece), except `branch`, which keeps
// all three: something between the accesses may change what a check reads,
// or the earlier one does not always run.

struct B {
    int *[[cccc::array, cccc::count(len)]] data;
    int len;
};

int g_len = 4;
int *[[cccc::array, cccc::count(g_len)]] g_data;

void touch(void) {}

int bound_written(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    int x = p[i];
    n     = 4;
    return x + p[i];
}

int index_written(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    int x = p[i];
    i     = 1;
    return x + p[i];
}

int bound_in_memory(struct B *b, int i) {
    int x = b->data[i];
    touch();
    return x + b->data[i];
}

int branch(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    int x = 0;
    if (n > 1)
        x = p[i];
    return x + p[i];
}

int rhs_effect(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    p[i] += (n = 4, 1);
    return 0;
}

int main(void) {
    int      a[4] = {1, 2, 3, 4};
    struct B b    = {a, 4};
    g_data        = a;
    int s         = bound_written(a, 4, 2) + index_written(a, 4, 2) +
                    bound_in_memory(&b, 2) + branch(a, 4, 2);
    rhs_effect(a, 4, 0);
    return s == 6 + 5 + 6 + 6 && a[0] == 2 ? 42 : 1;
}
