// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
enum Bad {
    BAD_A = 1,
};

enum Bad {
    BAD_A = 2,
};

int main(void) { return 42; }
