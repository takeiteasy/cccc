// CCCC_FLAGS: --checked-pointers
// A return bound spelled only on the prototype binds by parameter position,
// so a definition that renames the parameter still compiles and is checked.

int *[[cccc::array,
       cccc::count(n)]] head(int *[[cccc::array, cccc::count(n)]] p, int n);

int *head(int *q, int m) {
    (void)m;
    return q;
}

int main(void) {
    int a[2] = {41, 2};
    return head(a, 2)[0] + 1;
}
