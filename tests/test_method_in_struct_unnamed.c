// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a struct with methods needs a tag or a typedef name
struct {
    int x;
    int f(void *s) {
        return 1;
    }
} g;
int main(void) {
    return 0;
}
