// CCCC_FLAGS: -Wattributes --std=c23
// CCCC_EXPECT_STDERR: warning: unknown attribute 'foobar' ignored
int x [[foobar]];
int main(void) {
    return 42;
}
