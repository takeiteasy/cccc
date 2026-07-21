// CCCC_FLAGS: -Wdesignated-init --std=c23
// CCCC_REJECT_STDERR: designated_init
struct foo { int a, b; } __attribute__((designated_init));
int main(void) {
    struct foo y = {};
    (void)y;
    return 42;
}
