// CCCC_FLAGS: --bounds-checks --optimize=3
// CCCC_MATRIX_SKIP: depends on --optimize=3 (copy-prop specifically)
// Ticket #755: 3+ repeated p[const] reads of the same index in one function
// crashed with a host SIGSEGV under --bounds-checks --optimize=3. Root
// cause: CHKB (array bounds check) was missing from optimize.c's
// op_byte0_is_int_src() classifier, so copy-prop sub-pass B treated CHKB's
// byte-0 base-pointer read as a register *definition* and NOP'd the
// still-live MOV3 that fed the promoted restrict-free pointer into it,
// leaving CHKB/ADD3/CHKP3/LDR_W to run against a stale/garbage register on
// the third access onward.
static int f3(int *p) {
    int a = p[2];
    int b = p[2];
    int c = p[2];
    return a + b + c;
}

// n=4 and n=5 repeats (the ticket only confirmed a break point at n=3;
// verify the fix holds past it too).
static int f5(int *p) {
    int a = p[2];
    int b = p[2];
    int c = p[2];
    int d = p[2];
    int e = p[2];
    return a + b + c + d + e;
}

// Different constant indices per access -- the ticket noted this variant
// was "not yet checked"; same classifier bug applies since each access
// still lowers through CHKB.
static int fdiff(int *p) {
    int a = p[1];
    int b = p[2];
    int c = p[3];
    return a + b + c;
}

int main(void) {
    int arr[4] = {1, 2, 3, 4};
    if (f3(arr) != 9) return 1;
    if (f5(arr) != 15) return 2;
    if (fdiff(arr) != 9) return 3;
    return 42;
}
