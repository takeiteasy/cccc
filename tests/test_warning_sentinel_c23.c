// CCCC_FLAGS: -Wsentinel
// CCCC_EXPECT_STDERR: missing sentinel in function call
[[gnu::sentinel]] void foo(int a, ...);
void foo(int a, ...) { }
int main(void) {
    foo(1, 2, 3);
    return 42;
}
