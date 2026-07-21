// CCCC_FLAGS: -Wsentinel
// CCCC_EXPECT_STDERR: bare 0 is not a pointer
// A literal 0 that is not pointer-typed (int, not NULL/(void*)0/nullptr)
// still warns -- matching GCC's stricter -Wsentinel (#695).
void foo(int a, ...) __attribute__((sentinel));
void foo(int a, ...) { }
int main(void) {
    foo(1, 0);
    return 42;
}
