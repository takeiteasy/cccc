// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #943: CHKNT propagation also holds for an OPT (path-sensitive, #942)
// candidate, not just a FULL one -- on the run where `q` actually got
// rooted in the ntarray source `s`, a non-null write to the terminator
// slot traps.

void *malloc(unsigned long);

int main(void) {
    int n = 3;
    volatile int c = 1;
    void *buf = malloc(8);
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q = buf;
    if (c)
        q = s;
    q[3] = 'x'; // c is true this run -- q holds s, traps via CHKNT
    return 0;
}
