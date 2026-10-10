// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: non-static initialization of a flexible array member
//
// A non-empty flexible array initializer needs static storage, as in gcc and
// clang.
struct F {
    int n;
    int tail[];
};

int main(void) {
    struct F f = {1, {2, 3}};
    return f.n;
}
