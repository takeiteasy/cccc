// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: operator .\[\]. is already defined
typedef struct {
    int x;
} A;

[[cccc::operator([])]]
int *at_int(A *a, int i) {
    return &a->x;
}

[[cccc::operator([])]]
int *at_long(A *a, long i) {
    return &a->x;
}

int main(void) {
    return 0;
}
