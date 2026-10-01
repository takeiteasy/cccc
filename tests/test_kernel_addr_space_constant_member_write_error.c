// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: writing to constant memory
typedef struct {
    int n;
} P;
[[cccc::kernel]] static void k([[cccc::constant]] P *prm) {
    prm->n = 5;
}
int main(void) {
    return 42;
}
