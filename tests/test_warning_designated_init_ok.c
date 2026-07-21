// CCCC_FLAGS: -Wdesignated-init
// CCCC_REJECT_STDERR: designated_init
struct foo { int a, b; } __attribute__((designated_init));
int main(void) {
    struct foo v = {.a = 1, .b = 2};
    (void)v;
    return 42;
}
