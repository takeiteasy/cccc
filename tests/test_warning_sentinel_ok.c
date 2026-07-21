// CCCC_FLAGS: -Wsentinel
// CCCC_REJECT_STDERR: sentinel
void foo(int a, ...) __attribute__((sentinel));
void foo(int a, ...) { }
int main(void) {
    foo(1, 2, 3, (void*)0);
    foo(1, 0);
    return 42;
}
