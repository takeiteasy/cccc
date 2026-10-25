// CCCC_FLAGS: -Wchecked-calls
// CCCC_EXPECT_STDERR: warning: call to 'raw_len' with an unchecked pointer parameter 1.*\[-Wchecked-calls\]
// CCCC_EXPECT_STDERR: warning: call to 'raw_alloc' returns an unchecked pointer.*\[-Wchecked-calls\]
// Checked regions: a call from a checked region to a prototype with an
// unchecked pointer parameter or return type warns under -Wchecked-calls.

int raw_len(const char *s);
int *raw_alloc(int n);

int raw_len(const char *s) {
    return s ? 1 : 0;
}

int *raw_alloc(int n) {
    (void)n;
    return 0;
}

[[cccc::checked]]
int f(void) {
    int n = raw_len(0);
    raw_alloc(n);
    return 42;
}

int main(void) {
    return f();
}
