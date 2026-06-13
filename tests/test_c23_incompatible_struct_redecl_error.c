// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
struct Bad {
    int x;
};

struct Bad {
    long x;
};

int main(void) { return 42; }
