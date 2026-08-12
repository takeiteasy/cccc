// CCCC_FLAGS: -1
// CCCC_REJECT_STDOUT: MEMORY LEAK DETECTED
// A VLA's alloca-backed storage is automatic storage, not a user
// allocation -- it must never appear in the leak report (#979). A VLA
// inside a loop previously reported one phantom leak per iteration; a bare
// __builtin_alloca must be silent too.
int main(void) {
    int k = 3;
    for (int i = 0; i < 3; i++) {
        int u[k];
        u[0] = i;
    }
    char *p = __builtin_alloca(8);
    p[0] = 1;
    return 42;
}
