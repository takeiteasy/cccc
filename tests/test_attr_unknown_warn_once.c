// An unknown attribute in declarator-suffix position warns once, not once per
// speculative declarator parse.
// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: ^[^\n]*warning: unknown attribute 'bogus' ignored[^\n]*\n[^\n]*\n1 warning generated

void f(void) __attribute__((bogus));
void f(void) {}

int main(void) {
    f();
    return 42;
}
