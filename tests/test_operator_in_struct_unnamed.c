// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a struct with operators needs a tag or a typedef name
struct {
    int x;
    int operator + (void *a, void *b) {
        return 1;
    }
} g;
int main(void) {
    return 0;
}
