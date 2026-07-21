// CCCC_FLAGS: -Wsentinel
// CCCC_REJECT_STDERR: sentinel
void foo(int a, ...) __attribute__((sentinel(1)));
void foo(int a, ...) { }
int main(void) {
    foo(1, 2, (void*)0, 3);
    return 42;
}
