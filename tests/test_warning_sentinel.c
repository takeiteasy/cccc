// CCCC_FLAGS: -Wsentinel
// CCCC_EXPECT_STDERR: missing sentinel in function call
void foo(int a, ...) __attribute__((sentinel));
void foo(int a, ...) { }
int main(void) {
    foo(1, 2, 3);
    return 42;
}
