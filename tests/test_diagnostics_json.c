// JCC_FLAGS: -Wunused --json
// JCC_EXPECT_STDERR: "severity":"warning"
// JCC_EXPECT_STDERR: "option":"-Wunused"
// JCC_REJECT_STDERR: warnings generated.

int check(void) {
    int unused = 1;
    return 42;
}

int main(void) {
    return check();
}
