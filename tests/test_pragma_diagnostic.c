// JCC_FLAGS: -Wunused
// JCC_EXPECT_STDERR: unused variable 'y'
// JCC_REJECT_STDERR: unused variable 'x'

int check(void) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused"
    int x = 1;
#pragma GCC diagnostic pop
    int y = 2;
    return 42;
}

int main(void) {
    return check();
}
