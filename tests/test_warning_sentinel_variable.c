// CCCC_FLAGS: -Wsentinel
// CCCC_EXPECT_STDERR: missing sentinel in function call
// A variable that happens to hold NULL is not a literal sentinel -- only a
// literal/constant-folded NULL is accepted (GCC-aligned syntactic check).
void foo(int a, ...) __attribute__((sentinel));
void foo(int a, ...) {}
int main(void) {
    void *p = (void *)0;
    foo(1, p);
    return 42;
}
