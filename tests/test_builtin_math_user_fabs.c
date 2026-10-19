// CCCC_NATIVE_SKIP: the host builtin replaces a user fabs (#1715)
// A program's own fabs coexists with __builtin_fabs, which reaches libm.

double fabs(double x) {
    return 7.0;
}

volatile double arg = -1.0;

int main(void) {
    if (fabs(arg) != 7.0)
        return 1;
    if (__builtin_fabs(-42.0) != 42.0)
        return 2;
    return 42;
}
