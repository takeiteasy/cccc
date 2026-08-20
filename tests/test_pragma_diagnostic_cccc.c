// #pragma cccc diagnostic is the canonical cccc-namespaced form of the
// diagnostic push/pop/ignore/warning/error directives. It is consumed by the
// VM and not passed through in -c=generated output (unlike #pragma GCC
// diagnostic).

// CCCC_FLAGS: -Wunused
// CCCC_EXPECT_STDERR: unused variable 'y'
// CCCC_REJECT_STDERR: unused variable 'x'

int check(void) {
#pragma cccc diagnostic push
#pragma cccc diagnostic ignored "-Wunused"
    int x = 1;
#pragma cccc diagnostic pop
    int y = 2;
    return 42;
}

int main(void) {
    return check();
}
