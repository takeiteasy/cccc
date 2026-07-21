// CCCC_FLAGS: -Wsentinel
// CCCC_REJECT_STDERR: sentinel
// A C23 nullptr terminator is pointer-typed (nullptr_t) and is accepted
// silently, same as NULL / (void*)0 (#695).
void foo(int a, ...) __attribute__((sentinel));
void foo(int a, ...) { }
int main(void) {
    foo(1, nullptr);
    return 42;
}
