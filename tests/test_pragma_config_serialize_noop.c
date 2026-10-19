// CCCC_FLAGS: -m -Wignored-features
// CCCC_EXPECT_STDERR: config\(uaf_detection\) has no effect in -m mode
// CCCC_REJECT_STDERR: ignores VM runtime safety/debug options
// CCCC_EXPECT_STDOUT: int f\(int \*p\)
//
// Under -m, #pragma cccc config(...) of a VM-only flag is a warned no-op, as
// under -c=native.

#pragma cccc config(uaf_detection)

int f(int *p) {
    return p[0];
}

int main(void) {
    int x[3] = {1, 2, 3};
    return f(x) + 41;
}
