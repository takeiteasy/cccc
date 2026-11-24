// CCCC_FLAGS: -Wuser-defined-warnings
// CCCC_EXPECT_STDERR: warning: division by zero
// CCCC_EXPECT_STDERR: warning: always reported
// CCCC_REJECT_STDERR: not reported
//
// A diagnose_if warning is reported when its condition holds for a call's
// arguments, and also when the function is named without being called.

static int divide(int n, int d)
    __attribute__((diagnose_if(d == 0, "division by zero", "warning"))) {
    return n / (d ? d : 1);
}

static int noisy(void)
    __attribute__((diagnose_if(1, "always reported", "warning"))) {
    return 0;
}

static int quiet(int d)
    __attribute__((diagnose_if(d < 0, "not reported", "warning"))) {
    return d;
}

int main(int argc, char **argv) {
    int (*fp)(void) = noisy;
    return divide(84, 0) / 2 + quiet(argc) * 0 + fp() * 0;
}
