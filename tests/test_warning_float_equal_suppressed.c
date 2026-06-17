// CCCC_FLAGS: -Wno-float-equal
// CCCC_REJECT_STDERR: comparing floating-point values

int main(void) {
    double x = 1.0;
    double y = 1.0;
    (void)(x == y);
    return 42;
}
