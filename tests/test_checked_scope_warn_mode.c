// CCCC_FLAGS: -Wno-error=checked-regions
// CCCC_EXPECT_STDERR: warning: unchecked pointer local variable 'raw'.*\[-Wchecked-regions\]
// CCCC_EXPECT_STDERR: warning: cast to an unchecked pointer type.*\[-Wchecked-regions\]
// CCCC_EXPECT_STDERR: warning: implicit conversion from an unchecked pointer.*\[-Wchecked-regions\]
// Checked regions: -Wno-error=checked-regions downgrades every violation to a
// warning, so one build reports all of them and still runs.

int *g_raw;

[[cccc::checked]]
int f(int n) {
    int *raw                               = 0;
    int *[[cccc::array, cccc::count(n)]] a = (int[2]){40, 2};
    char *c                                = (char *)a;
    int *[[cccc::array, cccc::count(n)]] p = g_raw;
    (void)raw;
    (void)c;
    (void)p;
    return a[0] + a[1];
}

int main(void) {
    return f(2);
}
