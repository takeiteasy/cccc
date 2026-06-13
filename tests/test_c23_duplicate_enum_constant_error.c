// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
enum First {
    DUPLICATE = 1,
};

enum Second {
    DUPLICATE = 1,
};

int main(void) { return 42; }
