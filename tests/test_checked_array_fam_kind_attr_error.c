// EXPECT_COMPILE_ERROR
// Only count, byte_count and bounds take a bound after a checked array's `[]`.

struct S {
    int n;
    int d _Checked[] [[cccc::array]];
};

int main(void) {
    return 42;
}
