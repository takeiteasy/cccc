// CCCC_FLAGS: -Wmissing-prototypes
// CCCC_EXPECT_STDERR: no previous prototype for 'helper'.*\[-Wmissing-prototypes\]

int helper(int x) {
    return x + 1;
}

int main(void) {
    return helper(41);
}
