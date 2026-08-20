// CCCC_FLAGS: -Wmissing-declarations
// CCCC_EXPECT_STDERR: no previous declaration for
// 'helper'.*\[-Wmissing-declarations\]

int helper(int x) {
    return x + 1;
}

int main(void) {
    return helper(41);
}
