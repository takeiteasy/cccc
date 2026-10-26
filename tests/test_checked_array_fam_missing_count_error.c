// EXPECT_COMPILE_ERROR
// A checked flexible array member needs a count.

struct S {
    int n;
    int d _Checked[];
};

int main(void) {
    return 42;
}
