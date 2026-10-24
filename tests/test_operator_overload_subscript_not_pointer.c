// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must return a pointer
typedef struct {
    int x;
} A;

[[cccc::operator([])]]
int at(A *a, int i) {
    return a->x + i;
}

int main(void) {
    return 0;
}
