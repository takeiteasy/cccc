// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: methods are only allowed in file-scope structs and unions
int main(void) {
    struct S {
        int x;
        int f(struct S *s) {
            return s->x;
        }
    } s = {1};
    return s.f();
}
