// CCCC_FLAGS: -3
// Ticket #673: companion to test_dangling_deref_deeper_call.c -- the epoch
// check must not flag a &local passed as an out-param into a *deeper* call
// and dereferenced there, as long as the frame that created it (main's) is
// still alive. n's frame epoch stays in live_epochs for main()'s whole
// lifetime, so every deref of p inside use() must run to completion.
void use(int *p) {
    *p = 42;
}

int main(void) {
    int n = 0;
    use(&n);
    return n == 42 ? 42 : 1;
}
