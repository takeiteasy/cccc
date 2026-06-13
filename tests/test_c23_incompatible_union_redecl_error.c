// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
union Bad {
    int x;
};

union Bad {
    long x;
};

int main(void) { return 42; }
