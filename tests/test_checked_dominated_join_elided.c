// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_EXPECT_STDOUT: (CHKR .*){9}
// CCCC_REJECT_STDOUT: (CHKR .*){10}
//
// A check carries across `if` arms and joins, into loop bodies and conditions
// and past labels nothing jumps to. Each function keeps one check, except
// `both_arms`, which keeps one per arm.

int before_if(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = p[i];
    if (c)
        x += p[i];
    else
        x -= p[i];
    return x + p[i];
}

int both_arms(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x;
    if (c)
        x = p[i];
    else
        x = p[i] + 1;
    return x + p[i];
}

int early_return(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = p[i];
    if (c)
        return 0;
    return x + p[i];
}

int while_cond(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    int s = 0;
    while (p[i] > 0) {
        s += p[i];
        p[i]--;
    }
    return s;
}

int loop_reads(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int k) {
    int x = p[i];
    int s = 0;
    for (int j = 0; j < k; j++)
        s += p[i] + j;
    return s + x;
}

int do_body(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int k) {
    int s = 0;
    do {
        s += p[i];
        k--;
    } while (k > 0);
    return s + p[i];
}

int once(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = p[i];
    do {
        c = c + 1;
    } while (0);
    return x + p[i];
}

int plain_label(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    int x = p[i];
done:
    return x + p[i];
}

int main(void) {
    int a[4] = {1, 2, 3, 4};
    int b[4] = {1, 2, 3, 4};
    if (before_if(a, 4, 2, 1) != 9 || before_if(a, 4, 2, 0) != 3)
        return 1;
    if (both_arms(a, 4, 2, 1) != 6 || both_arms(a, 4, 2, 0) != 7)
        return 2;
    if (early_return(a, 4, 2, 0) != 6 || early_return(a, 4, 2, 1) != 0)
        return 3;
    if (while_cond(b, 4, 2) != 6 || b[2] != 0)
        return 4;
    if (loop_reads(a, 4, 2, 3) != 15)
        return 5;
    if (do_body(a, 4, 2, 2) != 9)
        return 6;
    if (once(a, 4, 2, 5) != 6 || plain_label(a, 4, 2) != 6)
        return 7;
    return 42;
}
