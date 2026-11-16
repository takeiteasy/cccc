// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no match

__attribute__((overloadable))
__attribute__((unavailable("no match")))
void f(...);
int __attribute__((overloadable)) f(int x) {
    return x;
}

int main(void) {
    return f(1, 2);
}
