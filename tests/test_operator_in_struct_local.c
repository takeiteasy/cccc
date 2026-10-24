// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: operators are only allowed in file-scope structs
void f(void) {
    struct V {
        int      x;
        struct V operator + (struct V a, struct V b) {
            return a;
        }
    } v;
}
int main(void) {
    return 0;
}
