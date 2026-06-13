// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: a declaration may not appear directly after a label
int main(void) {
    int v = 1;
    switch (v) {
        case 1:
            int x = 5;
            break;
    }
    return 0;
}
