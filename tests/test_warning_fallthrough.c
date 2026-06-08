// JCC_FLAGS: -Wextra --std=c23
// JCC_EXPECT_STDERR: warning: unannotated fallthrough between case labels
int test_fallthrough(int x) {
    int result = 0;
    switch (x) {
        case 1:
            result = 10;
        case 2:
            result = result + 20;
            break;
        default:
            result = 99;
    }
    return result;
}
int main(void) { test_fallthrough(1); return 42; }