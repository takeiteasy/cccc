// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expected '\)'
typedef struct {
    int x;
} A;

[[cccc::operator(<= >)]]
int a_cmp(const A *a, const A *b) {
    return a->x - b->x;
}

int main(void) {
    return 0;
}
