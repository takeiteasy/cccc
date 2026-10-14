// CCCC_FLAGS: --dangling-pointers
// A dead frame's tag on a slot must not trip a later frame's VLA pointer slot.
static int ln;
static void recv(void *p) {
    ln += 100;
}
static int mkv(void) {
    return 5;
}
static int sink(void *p) {
    return p != 0;
}
static int loop_var(void) {
    for (int v = mkv(); v < 6; v++) {
    }
    return 0;
}
static int escaped_var(void) {
    int v = mkv();
    return sink(&v);
}
static int vla_cleanup(int n) {
    __attribute__((cleanup(recv))) int a[n];
    return 0;
}
static int vla_plain(int n) {
    int a[n];
    a[0] = 1;
    return a[0] + n;
}
int main(void) {
    loop_var();
    vla_cleanup(3);
    loop_var();
    if (vla_plain(3) != 4)
        return 1;
    escaped_var();
    if (vla_plain(3) != 4)
        return 2;
    return ln == 100 ? 42 : 3;
}
