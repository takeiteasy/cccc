// An unknown attribute in the reserved `__gnu__::` scope warns once, naming
// the attribute rather than the scope.
// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: ^[^\n]*warning: unknown attribute 'bogus' ignored[^\n]*\n[^\n]*\n1 warning generated

[[__gnu__::bogus]] void f(void);
void f(void) {}

int main(void) {
    f();
    return 42;
}
