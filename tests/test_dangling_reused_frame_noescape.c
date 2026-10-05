// CCCC_FLAGS: --dangling-pointers
// A dead frame's tagged local must not trip a live frame's non-escaping array
// at the same address.
static int sink(void *p) {
    return p != 0;
}
static int f(int n) {
    int  a      = n;
    char pad[5] = {1, 2, 3, 4, 5};
    sink(&a);
    sink(pad);
    return a == n && pad[2] == 3;
}
static int deep(int d, int n) {
    char pad[5] = {1, 2, 3, 4, 5};
    return d ? deep(d - 1, n + pad[d % 5]) : f(n);
}
int main(void) {
    return (deep(3, 3) && deep(5, 5)) ? 42 : 1;
}
