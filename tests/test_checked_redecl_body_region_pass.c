// CCCC_FLAGS: --checked-pointers
// A [[cccc::checked]] definition accepts pointer parameters the prototype
// annotates.

int first(int *[[cccc::array, cccc::count(n)]] p, int n);

[[cccc::checked]] int first(int *p, int n) {
    (void)n;
    return p[0];
}

int main(void) {
    int a[2] = {42, 2};
    return first(a, 2);
}
