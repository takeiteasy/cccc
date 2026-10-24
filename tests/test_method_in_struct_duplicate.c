// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of S_f
typedef struct S {
    int x;
    int f(struct S *s) {
        return s->x;
    }
} S;
int S_f(S *s) {
    return 0;
}
int main(void) {
    return 0;
}
