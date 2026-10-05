// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: warning: unknown attribute 'bogus' ignored
int __attribute__((bogus)) x;
int main(void) {
    return 42;
}
