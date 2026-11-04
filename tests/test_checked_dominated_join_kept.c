// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_EXPECT_STDOUT: (CHKR .*){23}
// CCCC_REJECT_STDOUT: (CHKR .*){24}
//
// Each function keeps both checks (one apiece for `targeted_backward`): the
// access after the join, loop or label is not covered on every path to it.
// `break_exit` and `do_continue` need the state at the jump, not just the
// one at the loop's head; `targeted_forward` and `fallthrough` need the join
// at a label and at a `case`.

int g_len = 4;
int *[[cccc::array, cccc::count(g_len)]] g_data;

void cleanup_shrink(int *p) {
    g_len = 1;
}

int arm_writes_bound(int *[[cccc::array, cccc::count(n)]] p, int n, int i,
                     int c) {
    int x = p[i];
    if (c)
        n = 4;
    return x + p[i];
}

int loop_writes_bound(int *[[cccc::array, cccc::count(n)]] p, int n, int i,
                      int k) {
    int x = p[i];
    for (int j = 0; j < k; j++) {
        x += p[i];
        n  = n - 0;
    }
    return x;
}

int loop_writes_index(int *[[cccc::array, cccc::count(n)]] p, int n, int i,
                      int k) {
    int x = p[i];
    while (k-- > 0) {
        x += p[i];
        i++;
    }
    return x;
}

int break_path(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int k) {
    int x = p[i];
    while (k-- > 0) {
        if (k == 3) {
            n = 2;
            break;
        }
    }
    return x + p[i];
}

int break_exit(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    while (p[i] > 0) {
        n = 1;
        break;
    }
    return p[i];
}

int do_continue(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = 0;
    do {
        if (c) {
            n = 1;
            c = 0;
            continue;
        }
        x += p[i];
    } while (p[i] < 0);
    return x;
}

int targeted_forward(int *[[cccc::array, cccc::count(n)]] p, int n, int i,
                     int c) {
    int x = p[i];
    if (c) {
        n = 4;
        goto L;
    }
    x += 1;
L:
    return x + p[i];
}

int targeted_backward(int *[[cccc::array, cccc::count(n)]] p, int n, int i,
                      int k) {
    int x = 0;
L:
    x += p[i];
    if (k-- > 0)
        goto L;
    return x;
}

int switch_no_default(int *[[cccc::array, cccc::count(n)]] p, int n, int i,
                      int c) {
    int x = 0;
    switch (c) {
        case 1:
            x = p[i];
            break;
    }
    return x + p[i];
}

int fallthrough(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = 0;
    switch (c) {
        case 1:
            x = p[i];
        case 2:
            x += p[i];
    }
    return x;
}

int duff(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = p[i];
    switch (c) {
        case 0:
            for (;;) {
                x += p[i];
                case 1:
                    x += 1;
                    if (x > 20)
                        break;
            }
    }
    return x;
}

int cleanup_handler(int c) {
    int x = g_data[c];
    {
        int z __attribute__((cleanup(cleanup_shrink)))  = 0;
        x                                              += z;
    }
    return x + g_data[c];
}

int main(void) {
    int a[4] = {1, 2, 3, 4};
    g_data   = a;
    if (arm_writes_bound(a, 4, 2, 1) != 6)
        return 1;
    if (loop_writes_bound(a, 4, 2, 2) != 9)
        return 2;
    if (loop_writes_index(a, 4, 0, 2) != 4)
        return 3;
    if (break_path(a, 4, 0, 5) != 2)
        return 4;
    if (break_exit(a, 4, 0) != 1)
        return 10;
    if (do_continue(a, 4, 0, 1) != 0 || do_continue(a, 4, 0, 0) != 1)
        return 11;
    if (targeted_forward(a, 4, 2, 1) != 6 || targeted_forward(a, 4, 2, 0) != 7)
        return 5;
    if (targeted_backward(a, 4, 2, 2) != 9)
        return 6;
    if (switch_no_default(a, 4, 2, 1) != 6 ||
        switch_no_default(a, 4, 2, 0) != 3)
        return 7;
    if (duff(a, 4, 1, 0) != 23 || duff(a, 4, 1, 1) != 21)
        return 8;
    if (fallthrough(a, 4, 2, 1) != 6 || fallthrough(a, 4, 2, 2) != 3)
        return 12;
    if (cleanup_handler(0) != 2)
        return 9;
    return 42;
}
