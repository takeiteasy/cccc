// CCCC_FLAGS: --checked-pointers
// A plain definition after an annotated prototype with renamed parameters runs
// when its accesses stay within the prototype's bounds.

int sum(int *[[cccc::array, cccc::count(n)]] p, int n);

int sum(int *q, int m) {
    int total = 0;
    for (int i = 0; i < m; i++)
        total += q[i];
    return total;
}

int main(void) {
    int a[3] = {10, 12, 20};
    return sum(a, 3);
}
