// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: non-void aggregate function should return a value
struct Value {
    int value;
};

struct Value make_value(void) {
    return;
}

int main(void) {
    return 42;
}
