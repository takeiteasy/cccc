// CCCC_FLAGS: -Wduplicated-branches
// CCCC_EXPECT_STDERR: both branches of 'if' statement are
// identical.*\[-Wduplicated-branches\]

int foo(int x) {
    if (x > 0) {
        return 1;
    } else {
        return 1;
    }
    return 0;
}

int main(void) {
    return foo(1) + 41;
}
