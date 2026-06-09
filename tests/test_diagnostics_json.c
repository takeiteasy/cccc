// CCCC_FLAGS: -Wunused --json
// CCCC_EXPECT_STDERR: "severity":"warning"
// CCCC_EXPECT_STDERR: "option":"-Wunused"
// CCCC_REJECT_STDERR: warnings generated.

int check(void) {
    int unused = 1;
    return 42;
}

int main(void) {
    return check();
}
